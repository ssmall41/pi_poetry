#include "phrase_scanner/HumanReviewScanner.hpp"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>


std::vector<PhraseMatch> HumanReviewScanner::process_words(
    const std::vector<WordMatch>& word_stream) {

    // Work on a sorted copy so we can walk words left-to-right by position
    // without mutating the caller's vector.
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
        // Start a new phrase at position i.
        PhraseMatch phrase;
        phrase.start_offset = sorted[i]->start;
        phrase.words.push_back(sorted[i]->word);
        std::size_t prev_end = sorted[i]->start + sorted[i]->length;

        // Greedily extend the phrase as long as the next word begins exactly
        // where the previous one ends (gap == 0 means the words are
        // back-to-back in the pi digit sequence with no intervening digits).
        std::size_t j = i + 1;
        while (j < sorted.size()) {
            int gap = static_cast<int>(sorted[j]->start) -
                      static_cast<int>(prev_end);
            if (gap != 0) break;
            phrase.words.push_back(sorted[j]->word);
            prev_end = sorted[j]->start + sorted[j]->length;
            ++j;
        }

        result.push_back(std::move(phrase));

        // If we merged words i..j-1 into one phrase, jump past all of them;
        // otherwise just advance by one.
        i = j > i + 1 ? j : i + 1;
    }

    return result;
}

void HumanReviewScanner::process_words_streaming(
    const std::vector<WordMatch>& batch,
    const std::function<void(PhraseMatch)>& on_phrase) {

    // Words within each batch must arrive in ascending start order.
    // pending_phrase_ carries the in-progress phrase across batch boundaries,
    // so a phrase that spans two batches is assembled correctly.
    for (const auto& w : batch) {
        if (!pending_phrase_) {
            // No phrase in progress — start one with this word.
            pending_phrase_.emplace();
            pending_phrase_->start_offset = w.start;
            pending_phrase_->words.push_back(w.word);
            pending_end_ = w.start + w.length;
        } else {
            int gap = static_cast<int>(w.start) - static_cast<int>(pending_end_);
            if (gap != 0) {
                // This word is not adjacent to the in-progress phrase —
                // emit what we have and start fresh with this word.
                on_phrase(std::move(*pending_phrase_));
                pending_phrase_.emplace();
                pending_phrase_->start_offset = w.start;
                pending_phrase_->words.push_back(w.word);
                pending_end_ = w.start + w.length;
            } else {
                // Adjacent: extend the current phrase.
                pending_phrase_->words.push_back(w.word);
                pending_end_ = w.start + w.length;
            }
        }
    }
}

void HumanReviewScanner::flush_streaming(
    const std::function<void(PhraseMatch)>& on_phrase) {
    // The last phrase in a stream is never terminated by a gap, so it sits in
    // pending_phrase_ until the caller signals end-of-input by calling flush.
    if (pending_phrase_) {
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

