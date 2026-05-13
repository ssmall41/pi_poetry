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

struct WordPackage {
    std::size_t seq_id{0};
    std::size_t real_char_start{0};
    std::size_t real_char_end{0};
    // (global_offset, word) pairs; all start within [real_char_start, real_char_end)
    std::vector<std::pair<std::size_t, std::string>> raw_matches;
};

// One overlap-policy chain (from ETL or AllCombos) ready for phrase scanning.
// chain may include buffer words from the next chunk (start >= chunk_real_char_end).
// PhraseScannerWorker discards phrases whose first word starts >= chunk_real_char_end.
// seq_id is globally monotonic across all combo packages, used by ReorderBuffer.
struct ComboPackage {
    std::size_t seq_id{0};
    std::size_t chunk_real_char_end{0};
    std::vector<WordMatch> chain;
};

struct PhrasePackage {
    std::size_t seq_id{0};
    std::vector<PhraseMatch> phrases;
};
