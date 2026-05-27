#include "phrase_scanner/HumanReviewScanner.hpp"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

void HumanReviewScanner::set_min_phrase_length(std::size_t min_len) {
    min_phrase_length_ = min_len;
}

std::vector<PhraseMatch> HumanReviewScanner::process_words(
    const std::vector<WordMatch>& word_stream) {

    // Work on a sorted copy
    std::vector<const WordMatch*> sorted;
    sorted.reserve(word_stream.size());
    for (auto& w : word_stream) sorted.push_back(&w);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const WordMatch* a, const WordMatch* b) {
                         return a->start < b->start;
                     });

    std::vector<PhraseMatch> result;

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
            if (gap != 0) break;
            phrase.words.push_back(sorted[j]->word);
            prev_end = sorted[j]->start + sorted[j]->length;
            ++j;
        }

        if (phrase.words.size() >= min_phrase_length_) result.push_back(std::move(phrase));

        i = j > i + 1 ? j : i + 1;  // skip merged words
    }

    return result;
}

void HumanReviewScanner::process_words_streaming(
    const std::vector<WordMatch>& batch,
    const std::function<void(PhraseMatch)>& on_phrase) {

    for (const auto& w : batch) {
        if (!pending_phrase_) {
            pending_phrase_.emplace();
            pending_phrase_->start_offset = w.start;
            pending_phrase_->words.push_back(w.word);
            pending_end_ = w.start + w.length;
        } else {
            int gap = static_cast<int>(w.start) - static_cast<int>(pending_end_);
            if (gap != 0) {
                if (pending_phrase_->words.size() >= min_phrase_length_)
                    on_phrase(std::move(*pending_phrase_));
                pending_phrase_.emplace();
                pending_phrase_->start_offset = w.start;
                pending_phrase_->words.push_back(w.word);
                pending_end_ = w.start + w.length;
            } else {
                pending_phrase_->words.push_back(w.word);
                pending_end_ = w.start + w.length;
            }
        }
    }
}

void HumanReviewScanner::flush_streaming(
    const std::function<void(PhraseMatch)>& on_phrase) {
    if (pending_phrase_) {
        if (pending_phrase_->words.size() >= min_phrase_length_)
            on_phrase(std::move(*pending_phrase_));
        pending_phrase_.reset();
        pending_end_ = 0;
    }
}

void HumanReviewScanner::write_json_phrase(std::ostream& out, const PhraseMatch& p) {
    out << "{\"start_offset\":" << p.start_offset << ",\"words\":[";
    for (std::size_t i = 0; i < p.words.size(); ++i) {
        if (i > 0) out << ',';
        out << '"' << p.words[i] << '"';
    }
    out << "]}";
}

void HumanReviewScanner::write_json(const std::vector<PhraseMatch>& phrases,
                                    std::ostream& out) const {
    nlohmann::json j;
    j["phrases"] = nlohmann::json::array();
    for (const auto& p : phrases) {
        nlohmann::json entry;
        entry["start_offset"] = p.start_offset;
        entry["words"] = p.words;
        j["phrases"].push_back(std::move(entry));
    }
    out << j.dump(2) << '\n';
}

