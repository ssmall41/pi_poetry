#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct WordMatch {
    std::string word;
    std::size_t start;   // global character offset where the word begins
    std::size_t length;
    bool consecutive;    // true if this word starts exactly where the previous ended
};

enum class OverlapPolicy {
    EarliestThenLongest,
    AllCombos,
};

class WordFinder {
public:
    virtual ~WordFinder() = default;

    // Scans char_buffer (buf_len chars, global offset `offset`) and
    // returns word matches per the current overlap policy.
    // EarliestThenLongest returns one inner sequence; AllCombos returns all.
    virtual std::vector<std::vector<WordMatch>> scan(const char* char_buffer,
                                                     std::size_t buf_len,
                                                     std::size_t offset) = 0;

    // Loads a plain-text word list (one word per line).
    virtual void load_dictionary(const std::string& path) = 0;

    virtual void set_overlap_policy(OverlapPolicy policy) = 0;
    virtual std::size_t dropped_count() const = 0;
};
