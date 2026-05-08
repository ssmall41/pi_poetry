#include "pipeline/Pipeline.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <filesystem>

Pipeline::Pipeline(DigitSource& source, DigitMapper& mapper,
                   WordFinder& finder, PhraseScanner& scanner)
    : source_(source), mapper_(mapper), finder_(finder), scanner_(scanner) {}

void Pipeline::run(const std::filesystem::path& run_dir, bool write_letter_sequence) {
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

    if (write_letter_sequence) {
        auto letters_path = run_dir / "letter_sequence.txt";
        std::ofstream ofs(letters_path);
        if (!ofs)
            throw std::runtime_error("Cannot open output file: " + letters_path.string());
        ofs.write(chars.data(), static_cast<std::streamsize>(chars.size()));
        ofs.put('\n');
    }

    // Stage 3: find words
    auto word_matches = finder_.scan(chars.data(), chars.size(), 0);

    // Stage 4: collect phrases from every sequence (one for ETL, many for AllCombos)
    std::vector<PhraseMatch> all_phrases;
    for (const auto& seq : word_matches) {
        auto phrases = scanner_.process_words(seq);
        all_phrases.insert(all_phrases.end(), phrases.begin(), phrases.end());
    }

    // Sort by start_offset, then first word alphabetically, then full word list
    std::sort(all_phrases.begin(), all_phrases.end(), [](const PhraseMatch& a, const PhraseMatch& b) {
        if (a.start_offset != b.start_offset) return a.start_offset < b.start_offset;
        const std::string& wa = a.words.empty() ? "" : a.words[0];
        const std::string& wb = b.words.empty() ? "" : b.words[0];
        if (wa != wb) return wa < wb;
        return a.words < b.words;
    });

    // Remove exact duplicates (same start_offset and same word list)
    all_phrases.erase(
        std::unique(all_phrases.begin(), all_phrases.end(), [](const PhraseMatch& a, const PhraseMatch& b) {
            return a.start_offset == b.start_offset && a.words == b.words;
        }),
        all_phrases.end());

    scanner_.write_results(all_phrases, run_dir);
}
