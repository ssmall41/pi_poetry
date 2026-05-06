#include "word_finder/AhoCorasickCPU.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <limits>
#include <queue>
#include <string>

AhoCorasickCPU::AhoCorasickCPU() {
    nodes_.emplace_back();  // root node 0
}

void AhoCorasickCPU::insert_word(const std::string& word) {
    if (word.size() < min_word_length_) return;
    int cur = 0;
    for (char c : word) {
        if (c < 'a' || c > 'z') return;  // skip non-lowercase words
        int idx = c - 'a';
        if (nodes_[cur].children[idx] == -1) {
            nodes_[cur].children[idx] = static_cast<int>(nodes_.size());
            nodes_.emplace_back();
        }
        cur = nodes_[cur].children[idx];
    }
    nodes_[cur].output_word = word;
}

void AhoCorasickCPU::build() {
    // BFS to compute failure links (failure of root's children = root = 0)
    std::queue<int> q;
    for (int c = 0; c < 26; ++c) {
        int child = nodes_[0].children[c];
        if (child == -1) {
            nodes_[0].children[c] = 0;  // missing root child → loop to root
        } else {
            nodes_[child].failure = 0;
            q.push(child);
        }
    }
    while (!q.empty()) {
        int u = q.front(); q.pop();
        // output_link: points to nearest ancestor (via failure) with a match
        int f = nodes_[u].failure;
        nodes_[u].output_link = nodes_[f].output_word.empty()
                                    ? nodes_[f].output_link
                                    : f;
        for (int c = 0; c < 26; ++c) {
            int child = nodes_[u].children[c];
            if (child == -1) {
                // shortcut: goto(failure(u), c)
                nodes_[u].children[c] = nodes_[nodes_[u].failure].children[c];
            } else {
                nodes_[child].failure = nodes_[nodes_[u].failure].children[c];
                q.push(child);
            }
        }
    }
    built_ = true;
    prev_match_end_ = std::numeric_limits<std::size_t>::max();
}

void AhoCorasickCPU::load_dictionary(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open dictionary: " + path);
    std::string word;
    while (std::getline(f, word)) {
        if (!word.empty() && word.back() == '\r') word.pop_back();
        insert_word(word);
    }
}

void AhoCorasickCPU::set_overlap_policy(OverlapPolicy policy) {
    policy_ = policy;
}

void AhoCorasickCPU::set_min_word_length(std::size_t min_len) {
    min_word_length_ = min_len;
}

std::vector<WordMatch> AhoCorasickCPU::scan(const char* char_buffer,
                                             std::size_t buf_len,
                                             std::size_t offset) {
    assert(built_ && "build() must be called before scan()");

    // Collect all AC matches as (start_position, word)
    std::vector<std::pair<std::size_t, std::string>> raw;

    int state = 0;
    for (std::size_t i = 0; i < buf_len; ++i) {
        char c = char_buffer[i];
        if (c < 'a' || c > 'z') {
            state = 0;
            continue;
        }
        state = nodes_[state].children[c - 'a'];

        // Collect matches at this state and via output_link chain
        int s = state;
        while (s > 0) {
            if (!nodes_[s].output_word.empty()) {
                const std::string& w = nodes_[s].output_word;
                std::size_t start_pos = i + 1 - w.size();  // local position
                raw.emplace_back(start_pos, w);
            }
            s = nodes_[s].output_link;
            if (s <= 0) break;
        }
    }

    return apply_earliest_then_longest(raw, offset);
}

std::vector<WordMatch> AhoCorasickCPU::apply_earliest_then_longest(
    std::vector<std::pair<std::size_t, std::string>>& raw,
    std::size_t global_offset) {

    // Sort by start position ascending, then length descending
    std::sort(raw.begin(), raw.end(),
              [](const auto& a, const auto& b) {
                  if (a.first != b.first)
                      return a.first < b.first;
                  return a.second.size() > b.second.size();
              });

    std::vector<WordMatch> result;
    std::size_t scan_pos = 0;  // earliest position we can still accept

    for (auto& [local_start, word] : raw) {
        if (local_start < scan_pos) continue;  // consumed by earlier selection

        std::size_t global_start = global_offset + local_start;
        bool consecutive = (global_start == prev_match_end_);
        result.push_back({word, global_start, word.size(), consecutive});

        scan_pos = local_start + word.size();
        prev_match_end_ = global_start + word.size();
    }

    return result;
}
