#pragma once
#include "word_finder/WordFinder.hpp"
#include <filesystem>
#include <string>
#include <vector>

struct PhraseMatch {
    std::size_t start_offset;
    std::vector<std::string> words;
};

class PhraseScanner {
public:
    virtual ~PhraseScanner() = default;

    // Analyzes a word stream and returns all matches (including isolated
    // single words), sorted by start_offset ascending.
    virtual std::vector<PhraseMatch> process_words(
        const std::vector<WordMatch>& word_stream) = 0;
};
