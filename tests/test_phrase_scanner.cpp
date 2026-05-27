#include <gtest/gtest.h>
#include "phrase_scanner/HumanReviewScanner.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

WordMatch make_word(const std::string& w, std::size_t start,
                    bool consecutive = false) {
    return {w, start, w.size(), consecutive};
}

}  // namespace

TEST(HumanReviewScanner, SingleWordIsIsolated) {
    HumanReviewScanner hs;
    auto phrases = hs.process_words({make_word("cat", 0)});
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat"}));
    EXPECT_EQ(phrases[0].start_offset, 0u);
}

TEST(HumanReviewScanner, TwoAdjacentWords) {
    HumanReviewScanner hs;
    auto phrases = hs.process_words({
        make_word("cat", 0, false),
        make_word("dog", 3, true),
    });
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat", "dog"}));
}

TEST(HumanReviewScanner, TwoWordsWithNonZeroGapAreIsolated) {
    HumanReviewScanner hs;
    // cat ends at 3, dog starts at 9: gap = 9 - 3 = 6 — both isolated
    auto phrases = hs.process_words({
        make_word("cat", 0),
        make_word("dog", 9),
    });
    ASSERT_EQ(phrases.size(), 2u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat"}));
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_EQ(phrases[1].words, (std::vector<std::string>{"dog"}));
    EXPECT_EQ(phrases[1].start_offset, 9u);
}

TEST(HumanReviewScanner, TwoSeparatePhrases) {
    HumanReviewScanner hs;
    // first phrase: abc(3) at 0, def(3) at 3 — adjacent
    // second phrase: xyz(3) at 100, uvw(3) at 103 — adjacent
    // gap between def(end=6) and xyz(start=100) = 94 > 5 → separate phrases
    auto phrases = hs.process_words({
        make_word("abc", 0),
        make_word("def", 3),
        make_word("xyz", 100),
        make_word("uvw", 103),
    });
    ASSERT_EQ(phrases.size(), 2u);
    EXPECT_LT(phrases[0].start_offset, phrases[1].start_offset);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"abc", "def"}));
    EXPECT_EQ(phrases[1].words, (std::vector<std::string>{"xyz", "uvw"}));
}

TEST(HumanReviewScanner, PhrasePlusIsolatedWord) {
    HumanReviewScanner hs;
    // cat(0)+dog(3) form one phrase (consecutive); far(50) is isolated
    auto phrases = hs.process_words({
        make_word("cat", 0),
        make_word("dog", 3),
        make_word("far", 50),
    });
    ASSERT_EQ(phrases.size(), 2u);
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat", "dog"}));
    EXPECT_EQ(phrases[1].start_offset, 50u);
    EXPECT_EQ(phrases[1].words, (std::vector<std::string>{"far"}));
}

TEST(HumanReviewScanner, JsonOutputStructure) {
    HumanReviewScanner hs;
    auto phrases = hs.process_words({
        make_word("cat", 0, false),
        make_word("dog", 3, true),
    });
    std::ostringstream oss;
    hs.write_json(phrases, oss);
    auto j = nlohmann::json::parse(oss.str());
    ASSERT_TRUE(j.contains("phrases"));
    ASSERT_TRUE(j["phrases"].is_array());
    ASSERT_EQ(j["phrases"].size(), 1u);
    EXPECT_TRUE(j["phrases"][0].contains("start_offset"));
    EXPECT_TRUE(j["phrases"][0].contains("words"));
    EXPECT_EQ(j["phrases"][0]["start_offset"].get<std::size_t>(), 0u);
    EXPECT_EQ(j["phrases"][0]["words"].get<std::vector<std::string>>(),
              (std::vector<std::string>{"cat", "dog"}));
}

TEST(HumanReviewScanner, IsolatedWordJsonOutput) {
    HumanReviewScanner hs;
    auto phrases = hs.process_words({make_word("fox", 5)});
    std::ostringstream oss;
    hs.write_json(phrases, oss);
    auto j = nlohmann::json::parse(oss.str());
    ASSERT_EQ(j["phrases"].size(), 1u);
    EXPECT_EQ(j["phrases"][0]["words"].get<std::vector<std::string>>(),
              (std::vector<std::string>{"fox"}));
}

// ── Streaming API ────────────────────────────────────────────────────────────

TEST(HumanReviewScanner, StreamingEquivalence) {
    // Feeding words in batches must produce the same phrases as a single call.
    // Batch 1: cat+dog (form one phrase). Batch 2: far (isolated).
    std::vector<WordMatch> all_words = {
        make_word("cat",  0),
        make_word("dog",  3),
        make_word("far", 50),
    };

    HumanReviewScanner hs_stream;
    std::vector<PhraseMatch> streamed;
    auto on_phrase = [&](PhraseMatch p) { streamed.push_back(std::move(p)); };
    hs_stream.process_words_streaming({all_words[0], all_words[1]}, on_phrase);
    hs_stream.process_words_streaming({all_words[2]}, on_phrase);
    hs_stream.flush_streaming(on_phrase);

    HumanReviewScanner hs_full;
    auto full = hs_full.process_words(all_words);

    ASSERT_EQ(streamed.size(), full.size());
    for (std::size_t i = 0; i < full.size(); ++i) {
        EXPECT_EQ(streamed[i].start_offset, full[i].start_offset);
        EXPECT_EQ(streamed[i].words, full[i].words);
    }
}

TEST(HumanReviewScanner, StreamingFlush) {
    // A word sent via streaming must not be emitted until flush_streaming is called.
    HumanReviewScanner hs;
    std::vector<PhraseMatch> phrases;
    auto on_phrase = [&](PhraseMatch p) { phrases.push_back(std::move(p)); };

    hs.process_words_streaming({make_word("fox", 5)}, on_phrase);
    EXPECT_TRUE(phrases.empty());  // pending, not yet flushed

    hs.flush_streaming(on_phrase);
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].words,        (std::vector<std::string>{"fox"}));
    EXPECT_EQ(phrases[0].start_offset, 5u);
}

TEST(HumanReviewScanner, StreamingFinalizesOnNonZeroGap) {
    // When a new word has any gap from the previous word, the current phrase
    // is finalized immediately (before flush).
    HumanReviewScanner hs;
    std::vector<PhraseMatch> phrases;
    auto on_phrase = [&](PhraseMatch p) { phrases.push_back(std::move(p)); };

    // cat ends at 3; dog at 4 → gap = 1 → cat finalized when dog arrives
    hs.process_words_streaming({make_word("cat", 0), make_word("dog", 4)}, on_phrase);
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat"}));

    hs.flush_streaming(on_phrase);
    ASSERT_EQ(phrases.size(), 2u);
    EXPECT_EQ(phrases[1].words, (std::vector<std::string>{"dog"}));
}

TEST(HumanReviewScanner, StreamingBatchBoundaryDoesNotSplitPhrase) {
    // A phrase can span a batch boundary without being prematurely finalized.
    HumanReviewScanner hs;
    std::vector<PhraseMatch> phrases;
    auto on_phrase = [&](PhraseMatch p) { phrases.push_back(std::move(p)); };

    // cat+dog form one phrase (gap=0); split across two batches
    hs.process_words_streaming({make_word("cat", 0)}, on_phrase);
    EXPECT_TRUE(phrases.empty());
    hs.process_words_streaming({make_word("dog", 3)}, on_phrase);
    EXPECT_TRUE(phrases.empty());  // still pending
    hs.flush_streaming(on_phrase);
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat", "dog"}));
}

// ── min_phrase_length filter ──────────────────────────────────────────────────

TEST(HumanReviewScanner_MinPhraseLength, DefaultMin1KeepsSingleWord) {
    HumanReviewScanner hs;
    auto phrases = hs.process_words({make_word("cat", 0)});
    ASSERT_EQ(phrases.size(), 1u);
}

TEST(HumanReviewScanner_MinPhraseLength, Min2FiltersSingleWord_ProcessWords) {
    HumanReviewScanner hs;
    hs.set_min_phrase_length(2);
    auto phrases = hs.process_words({make_word("cat", 0)});
    EXPECT_TRUE(phrases.empty());
}

TEST(HumanReviewScanner_MinPhraseLength, Min2KeepsTwoWordPhrase_ProcessWords) {
    HumanReviewScanner hs;
    hs.set_min_phrase_length(2);
    auto phrases = hs.process_words({
        make_word("cat", 0, false),
        make_word("dog", 3, true),
    });
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat", "dog"}));
}

TEST(HumanReviewScanner_MinPhraseLength, Min2FiltersSingleKeepsLong_ProcessWords) {
    // [cat(0), dog(3,adj), far(50)] → cat+dog kept (2 words ≥ 2); far alone dropped
    HumanReviewScanner hs;
    hs.set_min_phrase_length(2);
    auto phrases = hs.process_words({
        make_word("cat", 0, false),
        make_word("dog", 3, true),
        make_word("far", 50, false),
    });
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat", "dog"}));
}

TEST(HumanReviewScanner_MinPhraseLength, Min2FiltersStreaming_SingleWordDropped) {
    HumanReviewScanner hs;
    hs.set_min_phrase_length(2);
    std::vector<PhraseMatch> emitted;
    auto on_phrase = [&](PhraseMatch p) { emitted.push_back(std::move(p)); };

    // cat ends at 3, dog starts at 4: gap=1 → cat finalized as 1-word phrase → dropped
    hs.process_words_streaming({make_word("cat", 0), make_word("dog", 4)}, on_phrase);
    EXPECT_TRUE(emitted.empty());
    hs.flush_streaming(on_phrase);
    // dog also alone → dropped
    EXPECT_TRUE(emitted.empty());
}

TEST(HumanReviewScanner_MinPhraseLength, Min2KeepsStreaming_TwoWordPhrase) {
    HumanReviewScanner hs;
    hs.set_min_phrase_length(2);
    std::vector<PhraseMatch> emitted;
    auto on_phrase = [&](PhraseMatch p) { emitted.push_back(std::move(p)); };

    hs.process_words_streaming({make_word("cat", 0)}, on_phrase);
    EXPECT_TRUE(emitted.empty());
    hs.process_words_streaming({make_word("dog", 3)}, on_phrase);
    EXPECT_TRUE(emitted.empty());  // still pending
    hs.flush_streaming(on_phrase);
    ASSERT_EQ(emitted.size(), 1u);
    EXPECT_EQ(emitted[0].words, (std::vector<std::string>{"cat", "dog"}));
}

TEST(HumanReviewScanner_MinPhraseLength, Min3FiltersStreaming_TwoWordPhraseDropped) {
    HumanReviewScanner hs;
    hs.set_min_phrase_length(3);
    std::vector<PhraseMatch> emitted;
    auto on_phrase = [&](PhraseMatch p) { emitted.push_back(std::move(p)); };

    hs.process_words_streaming({make_word("cat", 0), make_word("dog", 3)}, on_phrase);
    hs.flush_streaming(on_phrase);
    EXPECT_TRUE(emitted.empty());
}

