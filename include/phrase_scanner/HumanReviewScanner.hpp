#pragma once
#include "PhraseScanner.hpp"
#include <functional>
#include <iosfwd>
#include <optional>

class HumanReviewScanner final : public PhraseScanner {
public:
    std::vector<PhraseMatch> process_words(
        const std::vector<WordMatch>& word_stream) override;

    // Streaming phrase detection: feed ordered batches of word matches.
    // on_phrase is called for each phrase that is finalized.
    // Call flush_streaming() after the last batch to emit any pending phrase.
    void process_words_streaming(
        const std::vector<WordMatch>& batch,
        const std::function<void(PhraseMatch)>& on_phrase);
    void flush_streaming(const std::function<void(PhraseMatch)>& on_phrase);

    void set_min_phrase_length(std::size_t min_len);

    static void write_json_phrase(std::ostream& out, const PhraseMatch& p);
    void write_json(const std::vector<PhraseMatch>& phrases, std::ostream& out) const;

private:
    // Streaming state
    std::optional<PhraseMatch> pending_phrase_;
    std::size_t pending_end_{0};
    std::size_t min_phrase_length_{1};
};
