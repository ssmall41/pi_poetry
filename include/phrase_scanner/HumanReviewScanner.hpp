#pragma once
#include "PhraseScanner.hpp"
#include <functional>
#include <iosfwd>
#include <optional>

class HumanReviewScanner final : public PhraseScanner {
public:
    explicit HumanReviewScanner(int max_gap = 5);
    int max_gap() const { return max_gap_; }

    std::vector<PhraseMatch> process_words(
        const std::vector<WordMatch>& word_stream) override;

    void set_gap_policy(GapPolicy policy) override;

    void write_results(const std::vector<PhraseMatch>& phrases,
                       const std::filesystem::path& run_dir) const override;

    // Streaming phrase detection: feed ordered batches of word matches.
    // on_phrase is called for each phrase that is finalized.
    // Call flush_streaming() after the last batch to emit any pending phrase.
    void process_words_streaming(
        const std::vector<WordMatch>& batch,
        const std::function<void(PhraseMatch)>& on_phrase);
    void flush_streaming(const std::function<void(PhraseMatch)>& on_phrase);

    // Per-phrase stream writers (used by streaming pipeline and tests)
    static void write_text_phrase(std::ostream& out, const PhraseMatch& p);
    static void write_json_phrase(std::ostream& out, const PhraseMatch& p);

    // Bulk stream writers (used directly in tests)
    void write_text(const std::vector<PhraseMatch>& phrases, std::ostream& out) const;
    void write_json(const std::vector<PhraseMatch>& phrases, std::ostream& out) const;

private:
    int max_gap_;
    GapPolicy policy_{GapPolicy::GapTolerant};

    // Streaming state
    std::optional<PhraseMatch> pending_phrase_;
    std::size_t pending_end_{0};
};
