#pragma once
#include "PhraseScanner.hpp"
#include <iosfwd>

class HumanReviewScanner final : public PhraseScanner {
public:
    explicit HumanReviewScanner(int max_gap = 5);

    std::vector<PhraseMatch> process_words(
        const std::vector<WordMatch>& word_stream) override;

    void set_gap_policy(GapPolicy policy) override;

    void write_results(const std::vector<PhraseMatch>& phrases,
                       const std::filesystem::path& run_dir) const override;

    // Stream writers (used directly in tests)
    void write_text(const std::vector<PhraseMatch>& phrases, std::ostream& out) const;
    void write_json(const std::vector<PhraseMatch>& phrases, std::ostream& out) const;

private:
    int max_gap_;
    GapPolicy policy_{GapPolicy::GapTolerant};
};
