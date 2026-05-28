#pragma once
#include "WordFinder.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class AhoCorasickCPU final : public WordFinder {
public:
    AhoCorasickCPU();
    AhoCorasickCPU(AhoCorasickCPU&& other) noexcept
        : nodes_(std::move(other.nodes_)),
          policy_(other.policy_),
          min_word_length_(other.min_word_length_),
          min_phrase_length_(other.min_phrase_length_),
          max_word_len_(other.max_word_len_),
          built_(other.built_),
          dropped_count_(other.dropped_count_.load()) {}

    std::vector<std::vector<WordMatch>> scan(const char* char_buffer,
                                             std::size_t buf_len,
                                             std::size_t offset) override;
    void load_dictionary(const std::string& path) override;
    void set_overlap_policy(OverlapPolicy policy) override;
    void set_min_word_length(std::size_t min_len);
    void set_min_phrase_length(std::size_t min_len);

    // Insert a single word; call before build().
    void insert_word(const std::string& word);

    // Build the automaton (BFS failure links). Must be called after all
    // insert_word / load_dictionary calls and before scan().
    void build();

    // Stateful incremental scan. ac_state carries the current automaton node
    // between calls (pass 0 on the first call). Appends raw (global_start, word)
    // pairs to raw_out; global_offset is the character position of chunk[0].
    void scan_chunk(const char* chunk, std::size_t len,
                    std::size_t global_offset, int& ac_state,
                    std::vector<std::pair<std::size_t, std::string>>& raw_out) const;

    // Enumerate all non-overlapping word chains in raw (global-position pairs),
    // calling on_chain once per complete chain. Memory stays O(max_chain_depth).
    // on_chain is called 0–N times.
    void apply_all_combos_cb(
        const std::vector<std::pair<std::size_t, std::string>>& raw,
        std::size_t global_offset,
        const std::function<void(const std::vector<WordMatch>&)>& on_chain) const;

    // Apply ETL policy to raw (global-position pairs), calling on_chain 0–N times —
    // once per consecutive phrase group that meets min_phrase_length.
    void apply_etl_cb(
        const std::vector<std::pair<std::size_t, std::string>>& raw,
        std::size_t global_offset,
        const std::function<void(const std::vector<WordMatch>&)>& on_chain) const;

    // Dispatch to apply_etl_cb or apply_all_combos_cb based on the configured policy.
    void apply_policy_cb(
        const std::vector<std::pair<std::size_t, std::string>>& raw,
        std::size_t global_offset,
        const std::function<void(const std::vector<WordMatch>&)>& on_chain) const;

    OverlapPolicy get_overlap_policy() const { return policy_; }
    std::size_t max_word_length() const { return max_word_len_; }
    std::size_t dropped_count() const override { return dropped_count_.load(); }

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
    std::size_t min_word_length_{1};
    std::size_t min_phrase_length_{1};
    std::size_t max_word_len_{0};
    bool built_{false};
    mutable std::atomic<std::size_t> dropped_count_{0};

};
