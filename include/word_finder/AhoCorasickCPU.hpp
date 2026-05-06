#pragma once
#include "WordFinder.hpp"
#include <array>
#include <string>
#include <vector>

class AhoCorasickCPU final : public WordFinder {
public:
    AhoCorasickCPU();

    std::vector<WordMatch> scan(const char* char_buffer,
                                std::size_t buf_len,
                                std::size_t offset) override;
    void load_dictionary(const std::string& path) override;
    void set_overlap_policy(OverlapPolicy policy) override;

    // Insert a single word; call before build().
    void insert_word(const std::string& word);

    // Build the automaton (BFS failure links). Must be called after all
    // insert_word / load_dictionary calls and before scan().
    void build();

private:
    struct AcNode {
        std::array<int, 26> children{};
        int failure{0};
        int output_link{-1};
        std::string output_word;
        AcNode() { children.fill(-1); }
    };

    std::vector<AcNode> nodes_;
    OverlapPolicy policy_{OverlapPolicy::EarliestThenLongest};
    std::size_t prev_match_end_{0};
    bool built_{false};

    std::vector<WordMatch> apply_earliest_then_longest(
        std::vector<std::pair<std::size_t, std::string>>& raw,
        std::size_t global_offset);
};
