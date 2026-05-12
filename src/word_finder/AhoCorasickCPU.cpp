#include "word_finder/AhoCorasickCPU.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
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
    if (word.size() > max_word_len_) max_word_len_ = word.size();
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

std::vector<std::vector<WordMatch>> AhoCorasickCPU::scan(const char* char_buffer,
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

    if (policy_ == OverlapPolicy::AllCombos)
        return apply_all_combos(raw, offset);
    return apply_earliest_then_longest(raw, offset);
}

std::vector<std::vector<WordMatch>> AhoCorasickCPU::apply_earliest_then_longest(
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
        // prev_match_end_ is ETL-only state; not used by AllCombos
        bool consecutive = (global_start == prev_match_end_);
        result.push_back({word, global_start, word.size(), consecutive});

        scan_pos = local_start + word.size();
        prev_match_end_ = global_start + word.size();
    }

    return { result };
}

void AhoCorasickCPU::scan_chunk(const char* chunk, std::size_t len,
                                 std::size_t global_offset, int& ac_state,
                                 std::vector<std::pair<std::size_t, std::string>>& raw_out) const {
    assert(built_ && "build() must be called before scan_chunk()");
    for (std::size_t i = 0; i < len; ++i) {
        char c = chunk[i];
        if (c < 'a' || c > 'z') {
            ac_state = 0;
            continue;
        }
        ac_state = nodes_[ac_state].children[c - 'a'];

        int s = ac_state;
        while (s > 0) {
            if (!nodes_[s].output_word.empty()) {
                const std::string& w = nodes_[s].output_word;
                std::size_t global_start = global_offset + i + 1 - w.size();
                raw_out.emplace_back(global_start, w);
            }
            s = nodes_[s].output_link;
            if (s <= 0) break;
        }
    }
}

void AhoCorasickCPU::apply_all_combos_cb(
    const std::vector<std::pair<std::size_t, std::string>>& raw,
    std::size_t global_offset,
    const std::function<void(const std::vector<WordMatch>&)>& on_chain) const {

    std::map<std::size_t, std::vector<std::pair<std::string, std::size_t>>> by_start;
    for (const auto& [start, word] : raw)
        by_start[start].emplace_back(word, start + word.size());

    std::function<void(std::size_t, std::vector<WordMatch>&)> dfs =
        [&](std::size_t pos, std::vector<WordMatch>& current) {
            auto it = by_start.find(pos);
            if (it == by_start.end()) return;
            for (const auto& [word, end_pos] : it->second) {
                WordMatch m{word, global_offset + pos, word.size(), !current.empty()};
                current.push_back(m);
                on_chain(current);
                dfs(end_pos, current);
                current.pop_back();
            }
        };

    for (const auto& [start, _] : by_start) {
        std::vector<WordMatch> current;
        dfs(start, current);
    }
}

std::vector<WordMatch> AhoCorasickCPU::apply_etl(
    std::vector<std::pair<std::size_t, std::string>>& raw) {
    auto seqs = apply_earliest_then_longest(raw, 0);
    return seqs.empty() ? std::vector<WordMatch>{} : std::move(seqs[0]);
}

std::vector<std::vector<WordMatch>> AhoCorasickCPU::apply_all_combos(
    const std::vector<std::pair<std::size_t, std::string>>& raw,
    std::size_t global_offset) {

    std::vector<std::vector<WordMatch>> result;
    apply_all_combos_cb(raw, global_offset,
        [&](const std::vector<WordMatch>& chain) { result.push_back(chain); });
    return result;
}
