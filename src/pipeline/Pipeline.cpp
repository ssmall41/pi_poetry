#include "pipeline/Pipeline.hpp"
#include "pipeline/BoundedQueue.hpp"
#include "pipeline/WorkPackage.hpp"
#include "pipeline/StageWorker.hpp"
#include "pipeline/StageRunner.hpp"
#include "pipeline/ReorderBuffer.hpp"
#include "pipeline/DigitDispatcher.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
#include <vector>
#include <filesystem>

Pipeline::Pipeline(DigitSource& source, DigitMapper& mapper,
                   WordFinder& finder, PhraseScanner& scanner)
    : source_(source), mapper_(mapper), finder_(finder), scanner_(scanner) {}

void Pipeline::run(const std::filesystem::path& run_dir, bool write_letter_sequence,
                   std::size_t chunk_size) {
    if (source_.base() != mapper_.required_base())
        throw std::runtime_error("Base mismatch: digit source produces base " +
                                 std::to_string(source_.base()) +
                                 " but mapper requires base " +
                                 std::to_string(mapper_.required_base()));

    // Snap chunk_size to a multiple of digits_per_char so every chunk maps cleanly.
    const int dpc = mapper_.digits_per_char();
    if (chunk_size % static_cast<std::size_t>(dpc) != 0) {
        std::size_t orig = chunk_size;
        chunk_size = ((chunk_size / static_cast<std::size_t>(dpc)) + 1) *
                     static_cast<std::size_t>(dpc);
        std::cout << "chunk_size " << orig << " is not a multiple of digits_per_char ("
                  << dpc << "); snapped up to " << chunk_size << "\n";
    }

    auto* ac = dynamic_cast<AhoCorasickCPU*>(&finder_);
    if (!ac)
        throw std::runtime_error("Pipeline streaming requires AhoCorasickCPU");

    auto* hs = dynamic_cast<HumanReviewScanner*>(&scanner_);
    if (!hs)
        throw std::runtime_error("Pipeline streaming requires HumanReviewScanner");

    std::vector<uint8_t> digit_buf(chunk_size);
    std::vector<char>    char_buf(chunk_size / static_cast<std::size_t>(dpc));

    int ac_state = 0;
    std::size_t global_char_offset = 0;

    std::vector<std::pair<std::size_t, std::string>> raw_global;

    source_.reset();

    std::ofstream letters_out;
    if (write_letter_sequence) {
        auto p = run_dir / "letter_sequence.txt";
        letters_out.open(p);
        if (!letters_out)
            throw std::runtime_error("Cannot open output file: " + p.string());
    }

    while (true) {
        std::size_t n = source_.next_chunk(digit_buf.data(), chunk_size);
        if (n == 0) break;

        std::size_t usable = (n / static_cast<std::size_t>(dpc)) *
                             static_cast<std::size_t>(dpc);
        if (usable == 0) break;

        std::size_t n_chars = 0;
        mapper_.map(digit_buf.data(), usable, char_buf.data(), n_chars);

        if (write_letter_sequence)
            letters_out.write(char_buf.data(), static_cast<std::streamsize>(n_chars));

        ac->scan_chunk(char_buf.data(), n_chars, global_char_offset,
                       ac_state, raw_global);
        global_char_offset += n_chars;
    }

    if (write_letter_sequence)
        letters_out.put('\n');

    std::ofstream txt_out(run_dir / "results.txt");
    if (!txt_out)
        throw std::runtime_error("Cannot open output file: " +
                                 (run_dir / "results.txt").string());

    std::ofstream json_out(run_dir / "results.json");
    if (!json_out)
        throw std::runtime_error("Cannot open output file: " +
                                 (run_dir / "results.json").string());

    json_out << "{\n  \"phrases\": [";
    bool first_json = true;

    auto on_phrase = [&](PhraseMatch p) {
        HumanReviewScanner::write_text_phrase(txt_out, p);
        if (!first_json) json_out << ',';
        json_out << '\n';
        HumanReviewScanner::write_json_phrase(json_out, p);
        first_json = false;
    };

    auto emit_sequence = [&](const std::vector<WordMatch>& seq) {
        hs->process_words_streaming(seq, on_phrase);
        hs->flush_streaming(on_phrase);
    };

    if (ac->get_overlap_policy() == OverlapPolicy::AllCombos) {
        ac->apply_all_combos_cb(raw_global, 0,
            [&](const std::vector<WordMatch>& chain) { emit_sequence(chain); });
    } else {
        auto etl_result = ac->apply_etl(raw_global);
        emit_sequence(etl_result);
    }

    json_out << "\n  ]\n}\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Parallel pipeline implementation
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ── Stage workers ─────────────────────────────────────────────────────────────

class DigitMapperWorker final : public StageWorker<DigitPackage, LetterPackage> {
public:
    DigitMapperWorker(DigitMapper& mapper, int dpc) : mapper_(mapper), dpc_(dpc) {}
    std::string stage_name() const override { return "digit_mapper"; }

    LetterPackage process(DigitPackage pkg) override {
        std::size_t usable = (pkg.digits.size() / static_cast<std::size_t>(dpc_)) *
                             static_cast<std::size_t>(dpc_);
        std::vector<char> chars(usable / static_cast<std::size_t>(dpc_));
        std::size_t n_chars = 0;
        if (usable > 0)
            mapper_.map(pkg.digits.data(), usable, chars.data(), n_chars);
        chars.resize(n_chars);

        LetterPackage out;
        out.seq_id = pkg.seq_id;
        out.global_char_offset = pkg.global_digit_offset / static_cast<std::size_t>(dpc_);
        out.chars = std::move(chars);
        return out;
    }

private:
    DigitMapper& mapper_;
    int dpc_;
};

class WordFinderWorker final : public StageWorker<WFInput, WordPackage> {
public:
    explicit WordFinderWorker(AhoCorasickCPU& ac) : ac_(ac) {}
    std::string stage_name() const override { return "word_finder"; }

    WordPackage process(WFInput pkg) override {
        int state = 0;
        std::vector<std::pair<std::size_t, std::string>> raw;
        ac_.scan_chunk(pkg.chars.data(), pkg.chars.size(), pkg.real_char_start,
                       state, raw);

        // Discard matches that start in the lookahead buffer zone.
        raw.erase(std::remove_if(raw.begin(), raw.end(),
                      [&](const auto& p) { return p.first >= pkg.real_char_end; }),
                  raw.end());

        WordPackage out;
        out.seq_id = pkg.seq_id;
        out.real_char_start = pkg.real_char_start;
        out.real_char_end   = pkg.real_char_end;
        out.raw_matches     = std::move(raw);
        return out;
    }

private:
    AhoCorasickCPU& ac_;
};

class PhraseScannerWorker final : public StageWorker<ComboPackage, PhrasePackage> {
public:
    explicit PhraseScannerWorker(HumanReviewScanner& scanner) : scanner_(scanner) {}
    std::string stage_name() const override { return "phrase_scanner"; }

    PhrasePackage process(ComboPackage pkg) override {
        auto phrases = scanner_.process_words(pkg.chain);

        // Discard phrases whose first word starts in the buffer zone from the
        // next chunk (those will be output by the next chunk's phrase_scanner).
        phrases.erase(
            std::remove_if(phrases.begin(), phrases.end(),
                [&](const PhraseMatch& p) {
                    return p.start_offset >= pkg.chunk_real_char_end;
                }),
            phrases.end());

        PhrasePackage out;
        out.seq_id   = pkg.seq_id;
        out.phrases  = std::move(phrases);
        return out;
    }

private:
    HumanReviewScanner& scanner_;
};

// ── WFCoordinator: assembles letter packages with lookahead buffer ─────────────

void wf_coordinator(BoundedQueue<LetterPackage>& letter_q,
                    BoundedQueue<WFInput>& wf_in_q,
                    std::size_t max_word_len) {
    std::map<std::size_t, LetterPackage> pending;
    std::size_t next_emit = 0;
    // Buffer = max_word_len - 1 chars from the next chunk allows the AC scan
    // to complete words that start in the real range but extend into the next chunk.
    const std::size_t buf_chars = (max_word_len > 0) ? max_word_len - 1 : 0;

    auto emit = [&](std::size_t id, const LetterPackage* next_pkg) {
        auto& cur = pending.at(id);
        WFInput inp;
        inp.seq_id          = cur.seq_id;
        inp.real_char_start = cur.global_char_offset;
        inp.real_char_end   = cur.global_char_offset + cur.chars.size();
        inp.chars           = cur.chars;
        if (next_pkg && buf_chars > 0) {
            std::size_t take = std::min(buf_chars, next_pkg->chars.size());
            inp.chars.insert(inp.chars.end(),
                             next_pkg->chars.begin(),
                             next_pkg->chars.begin() + static_cast<std::ptrdiff_t>(take));
        }
        wf_in_q.push(std::move(inp));
        pending.erase(id);
    };

    LetterPackage pkg;
    while (letter_q.pop(pkg)) {
        pending[pkg.seq_id] = std::move(pkg);
        // Emit chunk N when both N and N+1 are present (N+1 provides the buffer).
        while (pending.count(next_emit) && pending.count(next_emit + 1)) {
            emit(next_emit, &pending.at(next_emit + 1));
            ++next_emit;
        }
    }
    // Flush the last chunk (no next chunk → no buffer).
    while (pending.count(next_emit)) {
        emit(next_emit, nullptr);
        ++next_emit;
    }
    wf_in_q.set_done();
}

// ── PhraseCoordinator: applies overlap policy and assembles phrase packages ───

void phrase_coordinator(BoundedQueue<WordPackage>& word_q,
                        BoundedQueue<ComboPackage>& combo_q,
                        AhoCorasickCPU& ac,
                        int max_gap) {
    std::map<std::size_t, WordPackage> pending;
    std::size_t next_emit    = 0;
    std::size_t global_combo = 0;

    const bool all_combos = (ac.get_overlap_policy() == OverlapPolicy::AllCombos);

    // ── ETL state ────────────────────────────────────────────────────────────
    // etl_scan_pos: global end-position of the last ETL-selected word.
    //   apply_etl resets its internal scan_pos on each call, so we carry this
    //   across chunks to block words that the serial greedy pass would skip.
    // pending_etl_chain: pre-computed ETL result for the next chunk, stored
    //   when chunk N's processing already computed chunk N+1's chain in order
    //   to determine the cross-boundary phrase extension.
    std::size_t etl_scan_pos = 0;
    std::optional<std::vector<WordMatch>> pending_etl_chain;

    auto emit_chunk = [&](std::size_t id,
                          const std::vector<std::pair<std::size_t, std::string>>* next_raw) {
        auto& pkg = pending.at(id);

        if (all_combos) {
            // ── AllCombos ────────────────────────────────────────────────────
            // Include ALL words from the next chunk so the DFS can follow
            // zero-gap consecutive chains across the boundary. Phrases that
            // start with a next-chunk word (start >= real_char_end) are
            // discarded by PhraseScannerWorker; the next chunk processes those
            // words independently, matching serial AllCombos exactly.
            std::vector<std::pair<std::size_t, std::string>> combined(
                pkg.raw_matches.begin(), pkg.raw_matches.end());
            if (next_raw)
                combined.insert(combined.end(), next_raw->begin(), next_raw->end());

            ac.apply_all_combos_cb(combined, 0,
                [&](const std::vector<WordMatch>& chain) {
                    ComboPackage cp;
                    cp.seq_id              = global_combo++;
                    cp.chunk_real_char_end = pkg.real_char_end;
                    cp.chain               = chain;
                    combo_q.push(std::move(cp));
                });
        } else {
            // ── ETL with streaming cross-boundary phrase extension ────────────
            // Step 1: obtain this chunk's ETL chain.
            //   If the previous call already computed it (stored in
            //   pending_etl_chain), reuse it; otherwise compute fresh.
            std::vector<WordMatch> chain_N;
            if (pending_etl_chain.has_value()) {
                chain_N = std::move(*pending_etl_chain);
                pending_etl_chain.reset();
            } else {
                std::vector<std::pair<std::size_t, std::string>> own;
                own.reserve(pkg.raw_matches.size());
                for (const auto& m : pkg.raw_matches)
                    if (m.first >= etl_scan_pos) own.push_back(m);
                chain_N = ac.apply_etl(own);
                if (!chain_N.empty())
                    etl_scan_pos = chain_N.back().start + chain_N.back().length;
            }

            // Step 2: compute next chunk's ETL chain and extend chain_N with
            //   its leading words that are within max_gap (cross-boundary
            //   phrase merging). Store the remainder for when chunk N+1 is
            //   processed so we never compute any chunk's ETL twice.
            if (next_raw) {
                std::vector<std::pair<std::size_t, std::string>> n1_own;
                n1_own.reserve(next_raw->size());
                for (const auto& m : *next_raw)
                    if (m.first >= etl_scan_pos) n1_own.push_back(m);
                auto chain_N1 = ac.apply_etl(n1_own);
                if (!chain_N1.empty())
                    etl_scan_pos = chain_N1.back().start + chain_N1.back().length;

                // Append leading words of chain_N1 that continue chain_N's phrase.
                std::size_t skip = 0;
                std::size_t prev_end = chain_N.empty()
                    ? 0 : chain_N.back().start + chain_N.back().length;
                for (const auto& wm : chain_N1) {
                    int gap = static_cast<int>(wm.start) - static_cast<int>(prev_end);
                    if (gap < 0 || gap > max_gap) break;
                    chain_N.push_back(wm);
                    prev_end = wm.start + wm.length;
                    ++skip;
                }
                // Store the rest of chain_N1 (words not given to chunk N).
                pending_etl_chain = std::vector<WordMatch>(
                    chain_N1.begin() + static_cast<std::ptrdiff_t>(skip),
                    chain_N1.end());
            }

            ComboPackage cp;
            cp.seq_id              = global_combo++;
            cp.chunk_real_char_end = pkg.real_char_end;
            cp.chain               = std::move(chain_N);
            combo_q.push(std::move(cp));
        }

        pending.erase(id);
    };

    WordPackage pkg;
    while (word_q.pop(pkg)) {
        pending[pkg.seq_id] = std::move(pkg);
        while (pending.count(next_emit) && pending.count(next_emit + 1)) {
            emit_chunk(next_emit, &pending.at(next_emit + 1).raw_matches);
            ++next_emit;
        }
    }
    while (pending.count(next_emit)) {
        emit_chunk(next_emit, nullptr);
        ++next_emit;
    }
    combo_q.set_done();
}

}  // namespace

void Pipeline::run_parallel(const std::filesystem::path& run_dir,
                             const ParallelConfig& cfg) {
    if (source_.base() != mapper_.required_base())
        throw std::runtime_error("Base mismatch");

    const int dpc = mapper_.digits_per_char();
    std::size_t chunk_size = cfg.chunk_size;
    if (chunk_size % static_cast<std::size_t>(dpc) != 0) {
        chunk_size = ((chunk_size / static_cast<std::size_t>(dpc)) + 1) *
                     static_cast<std::size_t>(dpc);
    }

    auto* ac = dynamic_cast<AhoCorasickCPU*>(&finder_);
    if (!ac)
        throw std::runtime_error("run_parallel requires AhoCorasickCPU");

    auto* hs = dynamic_cast<HumanReviewScanner*>(&scanner_);
    if (!hs)
        throw std::runtime_error("run_parallel requires HumanReviewScanner");

    source_.reset();

    const std::size_t Q = cfg.queue_capacity;
    BoundedQueue<DigitPackage>  digit_q(Q);
    BoundedQueue<LetterPackage> letter_q(Q);
    BoundedQueue<WFInput>       wf_in_q(Q);
    BoundedQueue<WordPackage>   word_q(Q);
    BoundedQueue<ComboPackage>  combo_q(Q);
    BoundedQueue<PhrasePackage> phrase_q(Q);

    // ── Stage 1: digit feeders ────────────────────────────────────────────────
    DigitDispatcher dispatcher(source_);
    std::atomic<int> active_feeders{cfg.digit_threads};
    std::mutex cout_mu;
    std::vector<std::thread> feeder_threads;

    for (int t = 0; t < cfg.digit_threads; ++t) {
        feeder_threads.emplace_back([&, t] {
            while (auto pkg = dispatcher.next(chunk_size)) {
                if (cfg.debug) {
                    auto remaining = digit_q.size();
                    std::lock_guard<std::mutex> lk(cout_mu);
                    std::cout << "[digit_source] worker " << t
                              << " claimed package " << pkg->seq_id
                              << " (" << remaining << " remaining)\n";
                }
                digit_q.push(std::move(*pkg));
            }
            if (active_feeders.fetch_sub(1) == 1)
                digit_q.set_done();
        });
    }

    // ── Stage 2: digit_mapper workers ────────────────────────────────────────
    std::vector<std::unique_ptr<StageWorker<DigitPackage, LetterPackage>>> mapper_workers;
    for (int i = 0; i < cfg.mapper_threads; ++i)
        mapper_workers.push_back(std::make_unique<DigitMapperWorker>(mapper_, dpc));
    StageRunner<DigitPackage, LetterPackage> mapper_runner(
        std::move(mapper_workers), digit_q, letter_q, cfg.debug);
    mapper_runner.start();

    // ── WFCoordinator: letter_q → wf_in_q (assembles lookahead buffer) ───────
    std::size_t max_word_len = ac->max_word_length();
    std::thread wf_coord_thread([&] {
        wf_coordinator(letter_q, wf_in_q, max_word_len);
    });

    // ── Stage 3: word_finder workers ─────────────────────────────────────────
    std::vector<std::unique_ptr<StageWorker<WFInput, WordPackage>>> finder_workers;
    for (int i = 0; i < cfg.finder_threads; ++i)
        finder_workers.push_back(std::make_unique<WordFinderWorker>(*ac));
    StageRunner<WFInput, WordPackage> finder_runner(
        std::move(finder_workers), wf_in_q, word_q, cfg.debug);
    finder_runner.start();

    // ── PhraseCoordinator: word_q → combo_q ──────────────────────────────────
    int max_gap = hs->max_gap();
    std::thread phrase_coord_thread([&] {
        phrase_coordinator(word_q, combo_q, *ac, max_gap);
    });

    // ── Stage 4: phrase_scanner workers ──────────────────────────────────────
    std::vector<std::unique_ptr<StageWorker<ComboPackage, PhrasePackage>>> scanner_workers;
    for (int i = 0; i < cfg.scanner_threads; ++i)
        scanner_workers.push_back(std::make_unique<PhraseScannerWorker>(*hs));
    StageRunner<ComboPackage, PhrasePackage> scanner_runner(
        std::move(scanner_workers), combo_q, phrase_q, cfg.debug);
    scanner_runner.start();

    // ── Writer thread: reorder buffer → disk ─────────────────────────────────
    std::ofstream txt_out(run_dir / "results.txt");
    if (!txt_out)
        throw std::runtime_error("Cannot open output file: " +
                                 (run_dir / "results.txt").string());
    std::ofstream json_out(run_dir / "results.json");
    if (!json_out)
        throw std::runtime_error("Cannot open output file: " +
                                 (run_dir / "results.json").string());

    json_out << "{\n  \"phrases\": [";
    bool first_json = true;

    std::thread writer_thread([&] {
        ReorderBuffer<PhrasePackage> reorder;
        PhrasePackage pp;
        while (phrase_q.pop(pp)) {
            reorder.submit(pp.seq_id, std::move(pp));
            reorder.drain([&](PhrasePackage& p) {
                for (auto& phrase : p.phrases) {
                    HumanReviewScanner::write_text_phrase(txt_out, phrase);
                    if (!first_json) json_out << ',';
                    json_out << '\n';
                    HumanReviewScanner::write_json_phrase(json_out, phrase);
                    first_json = false;
                }
            });
        }
        reorder.drain_all([&](PhrasePackage& p) {
            for (auto& phrase : p.phrases) {
                HumanReviewScanner::write_text_phrase(txt_out, phrase);
                if (!first_json) json_out << ',';
                json_out << '\n';
                HumanReviewScanner::write_json_phrase(json_out, phrase);
                first_json = false;
            }
        });
    });

    // ── Join all threads ──────────────────────────────────────────────────────
    for (auto& t : feeder_threads) t.join();
    mapper_runner.join();
    wf_coord_thread.join();
    finder_runner.join();
    phrase_coord_thread.join();
    scanner_runner.join();
    writer_thread.join();

    json_out << "\n  ]\n}\n";
}
