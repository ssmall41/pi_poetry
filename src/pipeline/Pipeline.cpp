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
#include <mutex>
#include <sstream>
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

    void process(DigitPackage pkg, const std::function<void(LetterPackage)>& emit) override {
        std::size_t usable = (pkg.digits.size() / static_cast<std::size_t>(dpc_)) *
                             static_cast<std::size_t>(dpc_);
        std::vector<char> chars(usable / static_cast<std::size_t>(dpc_));
        std::size_t n_chars = 0;
        if (usable > 0)
            mapper_.map(pkg.digits.data(), usable, chars.data(), n_chars);
        chars.resize(n_chars);

        LetterPackage out;
        out.seq_id             = pkg.seq_id;
        out.global_char_offset = pkg.global_digit_offset / static_cast<std::size_t>(dpc_);
        out.num_real_chars     = pkg.num_real_digits / static_cast<std::size_t>(dpc_);
        out.chars              = std::move(chars);
        emit(std::move(out));
    }

private:
    DigitMapper& mapper_;
    int dpc_;
};

class WordFinderWorker final : public StageWorker<LetterPackage, ComboPackage> {
public:
    explicit WordFinderWorker(AhoCorasickCPU& ac) : ac_(ac) {}
    std::string stage_name() const override { return "word_finder"; }

    void process(LetterPackage pkg, const std::function<void(ComboPackage)>& emit) override {
        int state = 0;
        std::vector<std::pair<std::size_t, std::string>> raw;
        const std::size_t real_char_start = pkg.global_char_offset;
        const std::size_t real_char_end   = pkg.global_char_offset + pkg.num_real_chars;
        ac_.scan_chunk(pkg.chars.data(), pkg.chars.size(), real_char_start, state, raw);

        // Exclude words that START in the lookahead zone — those belong to the
        // next chunk.  Words that start in the real zone but end in the lookahead
        // (boundary-straddling) are kept: the lookahead exists precisely for them.
        raw.erase(std::remove_if(raw.begin(), raw.end(),
                      [&](const auto& p) { return p.first >= real_char_end; }),
                  raw.end());

        const std::size_t chunk_id = pkg.seq_id;

        if (ac_.get_overlap_policy() == OverlapPolicy::AllCombos) {
            std::vector<ComboPackage> pending;
            std::size_t intra = 0;
            ac_.apply_all_combos_cb(raw, 0, [&](const std::vector<WordMatch>& chain) {
                ComboPackage cp;
                cp.chunk_id               = chunk_id;
                cp.intra_chunk_seq_id     = intra++;
                cp.final_package_in_chunk = false;
                cp.chain                  = chain;
                pending.push_back(std::move(cp));
            });
            if (pending.empty()) {
                ComboPackage cp;
                cp.chunk_id               = chunk_id;
                cp.intra_chunk_seq_id     = 0;
                cp.final_package_in_chunk = true;
                emit(std::move(cp));
            } else {
                pending.back().final_package_in_chunk = true;
                for (auto& cp : pending) emit(std::move(cp));
            }
        } else {
            ComboPackage cp;
            cp.chunk_id               = chunk_id;
            cp.intra_chunk_seq_id     = 0;
            cp.final_package_in_chunk = true;
            cp.chain                  = ac_.apply_etl(raw);
            emit(std::move(cp));
        }
    }

private:
    AhoCorasickCPU& ac_;
};

class PhraseScannerWorker final : public StageWorker<ComboPackage, PhrasePackage> {
public:
    explicit PhraseScannerWorker(HumanReviewScanner& scanner) : scanner_(scanner) {}
    std::string stage_name() const override { return "phrase_scanner"; }

    void process(ComboPackage pkg, const std::function<void(PhrasePackage)>& emit) override {
        PhrasePackage out;
        out.chunk_id               = pkg.chunk_id;
        out.intra_chunk_seq_id     = pkg.intra_chunk_seq_id;
        out.final_package_in_chunk = pkg.final_package_in_chunk;
        auto phrases = scanner_.process_words(pkg.chain);
        out.text_strs.reserve(phrases.size());
        out.json_strs.reserve(phrases.size());
        for (const auto& p : phrases) {
            std::ostringstream t, j;
            HumanReviewScanner::write_text_phrase(t, p);
            HumanReviewScanner::write_json_phrase(j, p);
            out.text_strs.push_back(t.str());
            out.json_strs.push_back(j.str());
        }
        emit(std::move(out));
    }

private:
    HumanReviewScanner& scanner_;
};

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

    BoundedQueue<DigitPackage>  digit_q(cfg.digit_q_capacity);
    BoundedQueue<LetterPackage> letter_q(cfg.letter_q_capacity);
    BoundedQueue<ComboPackage>  combo_q(cfg.combo_q_capacity);
    BoundedQueue<PhrasePackage> phrase_q(cfg.phrase_q_capacity);

    // ── Stage 1: digit feeders ────────────────────────────────────────────────
    const std::size_t max_word_len = ac->max_word_length();
    const std::size_t lookahead_digits =
        (max_word_len > 0 ? max_word_len - 1 : 0) * static_cast<std::size_t>(dpc);
    DigitDispatcher dispatcher(source_, chunk_size, lookahead_digits);
    std::atomic<int> active_feeders{cfg.digit_threads};
    std::mutex cout_mu;
    std::vector<std::thread> feeder_threads;

    for (int t = 0; t < cfg.digit_threads; ++t) {
        feeder_threads.emplace_back([&, t] {
            while (auto pkg = dispatcher.next()) {
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

    // ── Stage 3: word_finder workers (scan + apply overlap policy) ───────────
    std::vector<std::unique_ptr<StageWorker<LetterPackage, ComboPackage>>> finder_workers;
    for (int i = 0; i < cfg.finder_threads; ++i)
        finder_workers.push_back(std::make_unique<WordFinderWorker>(*ac));
    StageRunner<LetterPackage, ComboPackage> finder_runner(
        std::move(finder_workers), letter_q, combo_q, cfg.debug);
    finder_runner.start();

    // ── Stage 4: phrase_scanner workers ──────────────────────────────────────
    std::vector<std::unique_ptr<StageWorker<ComboPackage, PhrasePackage>>> scanner_workers;
    for (int i = 0; i < cfg.scanner_threads; ++i)
        scanner_workers.push_back(std::make_unique<PhraseScannerWorker>(*hs));
    StageRunner<ComboPackage, PhrasePackage> scanner_runner(
        std::move(scanner_workers), combo_q, phrase_q, cfg.debug);
    scanner_runner.start();

    // ── Writer thread: reorder buffer → disk ─────────────────────────────────
    const std::string txt_path  = cfg.dry_run ? "/dev/null" : (run_dir / "results.txt").string();
    const std::string json_path = cfg.dry_run ? "/dev/null" : (run_dir / "results.json").string();
    std::ofstream txt_out(txt_path);
    if (!txt_out)
        throw std::runtime_error("Cannot open output file: " + txt_path);
    std::ofstream json_out(json_path);
    if (!json_out)
        throw std::runtime_error("Cannot open output file: " + json_path);

    json_out << "{\n  \"phrases\": [";
    bool first_json = true;

    std::thread writer_thread([&] {
        ReorderBuffer<PhrasePackage> reorder;
        PhrasePackage pp;
        while (phrase_q.pop(pp)) {
            reorder.submit(pp.chunk_id, pp.intra_chunk_seq_id,
                           pp.final_package_in_chunk, std::move(pp));
            reorder.drain([&](PhrasePackage& p) {
                for (std::size_t i = 0; i < p.text_strs.size(); ++i) {
                    txt_out << p.text_strs[i];
                    if (!first_json) json_out << ',';
                    json_out << '\n' << p.json_strs[i];
                    first_json = false;
                }
            });
        }
        reorder.drain_all([&](PhrasePackage& p) {
            for (std::size_t i = 0; i < p.text_strs.size(); ++i) {
                txt_out << p.text_strs[i];
                if (!first_json) json_out << ',';
                json_out << '\n' << p.json_strs[i];
                first_json = false;
            }
        });
    });

    // ── Join all threads ──────────────────────────────────────────────────────
    for (auto& t : feeder_threads) t.join();
    mapper_runner.join();
    finder_runner.join();
    scanner_runner.join();
    writer_thread.join();

    json_out << "\n  ]\n}\n";
}
