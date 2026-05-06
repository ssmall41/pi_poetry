#include "pipeline/Pipeline.hpp"
#include <stdexcept>
#include <vector>

Pipeline::Pipeline(DigitSource& source, DigitMapper& mapper,
                   WordFinder& finder, PhraseScanner& scanner)
    : source_(source), mapper_(mapper), finder_(finder), scanner_(scanner) {}

void Pipeline::run(const std::string& output_text_path,
                   const std::string& output_json_path) {
    if (source_.base() != mapper_.required_base())
        throw std::runtime_error("Base mismatch: digit source produces base " +
                                 std::to_string(source_.base()) +
                                 " but mapper requires base " +
                                 std::to_string(mapper_.required_base()));

    // Stage 1: read all digits
    source_.reset();
    std::vector<uint8_t> all_digits;
    if (auto len = source_.estimated_length())
        all_digits.reserve(*len);

    std::vector<uint8_t> chunk(kChunkSize);
    std::size_t n;
    while ((n = source_.next_chunk(chunk.data(), kChunkSize)) > 0)
        all_digits.insert(all_digits.end(), chunk.begin(), chunk.begin() + n);

    // Stage 2: map digits to characters (only use complete pairs)
    const int dpc = mapper_.digits_per_char();
    const std::size_t usable = (all_digits.size() / dpc) * dpc;
    std::vector<char> chars(usable / dpc);
    std::size_t out_n = 0;
    mapper_.map(all_digits.data(), usable, chars.data(), out_n);
    chars.resize(out_n);

    // Stage 3: find words
    auto word_matches = finder_.scan(chars.data(), chars.size(), 0);

    // Stage 4: find phrases and write output
    auto phrase_matches = scanner_.process_words(word_matches);
    scanner_.write_results(phrase_matches, output_text_path, output_json_path);
}
