#pragma once
#include "word_finder/WordFinder.hpp"
#include <string>
#include <vector>

struct PhraseMatch {
    std::size_t start_offset;
    std::vector<std::string> words;
    std::vector<int> gap_sizes;  // gap in chars between consecutive words
};

enum class GapPolicy {
    Strict,       // zero gaps allowed
    GapTolerant,  // MVP: gaps up to max_gap allowed
};

class PhraseScanner {
public:
    virtual ~PhraseScanner() = default;

    // Analyzes a word stream and returns all matches (including isolated
    // single words), sorted by start_offset ascending.
    virtual std::vector<PhraseMatch> process_words(
        const std::vector<WordMatch>& word_stream) = 0;

    virtual void set_gap_policy(GapPolicy policy) = 0;

    // Writes results to plain-text and JSON files.
    // On the interface to avoid Pipeline downcasting.
    virtual void write_results(const std::vector<PhraseMatch>& phrases,
                               const std::string& text_path,
                               const std::string& json_path) const = 0;
};
