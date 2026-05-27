#pragma once
#include "word_finder/WordFinder.hpp"
#include "phrase_scanner/PhraseScanner.hpp"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct DigitPackage {
    std::size_t seq_id{0};
    std::size_t global_digit_offset{0};
    std::size_t num_real_digits{0};
    std::vector<uint8_t> digits;
};

struct LetterPackage {
    std::size_t seq_id{0};
    std::size_t global_char_offset{0};
    std::size_t num_real_chars{0};
    std::vector<char> chars;   // real chars + lookahead buffer
};

// One overlap-policy chain (from ETL or AllCombos) ready for phrase scanning.
// chunk_id matches the LetterPackage seq_id this chain came from.
// intra_chunk_seq_id is 0-based within the chunk; final_package_in_chunk marks
// the last package for that chunk so ReorderBuffer can drain in order.
struct ComboPackage {
    std::size_t chunk_id{0};
    std::size_t intra_chunk_seq_id{0};
    bool        final_package_in_chunk{false};
    std::vector<WordMatch> chain;
    // Non-empty only on final_package_in_chunk when write_letters is enabled.
    // Contains real chars only (no lookahead); size == num_real_letter_chars.
    std::vector<char> letter_chars;
    std::size_t       num_real_letter_chars{0};
};

struct PhrasePackage {
    std::size_t chunk_id{0};
    std::size_t intra_chunk_seq_id{0};
    bool        final_package_in_chunk{false};
    std::vector<std::string> json_strs;  // one JSON object per phrase, no comma/newline
    std::vector<char> letter_chars;
    std::size_t       num_real_letter_chars{0};
};
