#include "phrase_scanner/HumanReviewScanner.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

HumanReviewScanner::HumanReviewScanner(int max_gap) : max_gap_(max_gap) {}

void HumanReviewScanner::set_gap_policy(GapPolicy policy) {
    policy_ = policy;
}

std::vector<PhraseMatch> HumanReviewScanner::process_words(
    const std::vector<WordMatch>& word_stream) {

    if (word_stream.size() < 2) return {};

    // Work on a sorted copy
    std::vector<const WordMatch*> sorted;
    sorted.reserve(word_stream.size());
    for (auto& w : word_stream) sorted.push_back(&w);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const WordMatch* a, const WordMatch* b) {
                         return a->start < b->start;
                     });

    std::vector<PhraseMatch> result;

    // Greedy merge: build phrases by extending as long as gap <= max_gap_
    std::size_t i = 0;
    while (i < sorted.size()) {
        PhraseMatch phrase;
        phrase.start_offset = sorted[i]->start;
        phrase.words.push_back(sorted[i]->word);
        std::size_t prev_end = sorted[i]->start + sorted[i]->length;

        std::size_t j = i + 1;
        while (j < sorted.size()) {
            int gap = static_cast<int>(sorted[j]->start) -
                      static_cast<int>(prev_end);
            if (gap < 0 || gap > max_gap_) break;
            phrase.gap_sizes.push_back(gap);
            phrase.words.push_back(sorted[j]->word);
            prev_end = sorted[j]->start + sorted[j]->length;
            ++j;
        }

        if (phrase.words.size() >= 2)
            result.push_back(std::move(phrase));

        i = j > i + 1 ? j : i + 1;  // skip merged words
    }

    return result;
}

void HumanReviewScanner::write_text(const std::vector<PhraseMatch>& phrases,
                                    std::ostream& out) const {
    for (const auto& p : phrases) {
        out << "Offset " << p.start_offset << ": ";
        for (std::size_t i = 0; i < p.words.size(); ++i) {
            if (i > 0) out << ' ';
            out << p.words[i];
        }
        if (!p.gap_sizes.empty()) {
            out << " [gaps:";
            for (int g : p.gap_sizes) out << ' ' << g;
            out << ']';
        }
        out << '\n';
    }
}

void HumanReviewScanner::write_json(const std::vector<PhraseMatch>& phrases,
                                    std::ostream& out) const {
    nlohmann::json j;
    j["phrases"] = nlohmann::json::array();
    for (const auto& p : phrases) {
        nlohmann::json entry;
        entry["start_offset"] = p.start_offset;
        entry["words"] = p.words;
        entry["gap_sizes"] = p.gap_sizes;
        j["phrases"].push_back(std::move(entry));
    }
    out << j.dump(2) << '\n';
}

void HumanReviewScanner::write_results(const std::vector<PhraseMatch>& phrases,
                                       const std::string& text_path,
                                       const std::string& json_path) const {
    {
        std::ofstream f(text_path);
        if (!f) throw std::runtime_error("Cannot open output file: " + text_path);
        write_text(phrases, f);
    }
    {
        std::ofstream f(json_path);
        if (!f) throw std::runtime_error("Cannot open output file: " + json_path);
        write_json(phrases, f);
    }
}
