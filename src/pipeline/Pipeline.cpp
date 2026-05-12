#include "pipeline/Pipeline.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
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

    // AhoCorasickCPU is the only supported implementation (enforced by config).
    auto* ac = dynamic_cast<AhoCorasickCPU*>(&finder_);
    if (!ac)
        throw std::runtime_error("Pipeline streaming requires AhoCorasickCPU");

    auto* hs = dynamic_cast<HumanReviewScanner*>(&scanner_);
    if (!hs)
        throw std::runtime_error("Pipeline streaming requires HumanReviewScanner");

    // Buffers: digit_buf holds one chunk; char_buf holds mapped chars for that chunk.
    std::vector<uint8_t> digit_buf(chunk_size);
    std::vector<char>    char_buf(chunk_size / static_cast<std::size_t>(dpc));

    // Per-chunk carry state.
    int ac_state = 0;
    std::size_t global_char_offset = 0;

    // All raw (global_start, word) matches — accumulated across chunks.
    // O(total_matches) memory, much smaller than the char buffer.
    std::vector<std::pair<std::size_t, std::string>> raw_global;

    source_.reset();

    // Open letter_sequence file early if requested.
    std::ofstream letters_out;
    if (write_letter_sequence) {
        auto p = run_dir / "letter_sequence.txt";
        letters_out.open(p);
        if (!letters_out)
            throw std::runtime_error("Cannot open output file: " + p.string());
    }

    // Stage 1+2+3 loop: read digits → map → stateful AC scan.
    while (true) {
        std::size_t n = source_.next_chunk(digit_buf.data(), chunk_size);
        if (n == 0) break;

        // Drop trailing digits that don't form a complete pair.
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

    // Open streaming output files.
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

    // Stage 4: apply overlap policy, stream phrases to disk immediately.
    auto on_phrase = [&](PhraseMatch p) {
        HumanReviewScanner::write_text_phrase(txt_out, p);
        if (!first_json) json_out << ',';
        json_out << '\n';
        HumanReviewScanner::write_json_phrase(json_out, p);
        first_json = false;
    };

    auto emit_sequence = [&](const std::vector<WordMatch>& seq) {
        hs->process_words_streaming(seq, on_phrase);
        // Flush between sequences so each chain's phrases are finalized
        // before the next chain starts at a potentially earlier offset.
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
