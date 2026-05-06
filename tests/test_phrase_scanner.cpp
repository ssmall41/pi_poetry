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
    EXPECT_TRUE(phrases[0].gap_sizes.empty());
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
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0}));
}

TEST(HumanReviewScanner, GapExactlyAtLimit) {
    HumanReviewScanner hs;  // default max_gap = 5
    // cat ends at 3, dog starts at 8: gap = 8 - 3 = 5 (== G, included)
    auto phrases = hs.process_words({
        make_word("cat", 0),
        make_word("dog", 8),
    });
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{5}));
}

TEST(HumanReviewScanner, TwoWordsGapBeyondLimitBothIsolated) {
    HumanReviewScanner hs;
    // cat ends at 3, dog starts at 9: gap = 9 - 3 = 6 > 5 — both isolated
    auto phrases = hs.process_words({
        make_word("cat", 0),
        make_word("dog", 9),
    });
    ASSERT_EQ(phrases.size(), 2u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat"}));
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_TRUE(phrases[0].gap_sizes.empty());
    EXPECT_EQ(phrases[1].words, (std::vector<std::string>{"dog"}));
    EXPECT_EQ(phrases[1].start_offset, 9u);
    EXPECT_TRUE(phrases[1].gap_sizes.empty());
}

TEST(HumanReviewScanner, ThreeWordPhraseWithMixedGaps) {
    HumanReviewScanner hs;
    // meh(3) at 113 ends at 116; tut(3) at 121 gap=5; joe(3) at 124 gap=0
    auto phrases = hs.process_words({
        make_word("meh", 113),
        make_word("tut", 121),
        make_word("joe", 124),
    });
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"meh", "tut", "joe"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{5, 0}));
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
    // cat(0)+dog(3) form one phrase (gap=0 ≤ max_gap); far(50) is isolated
    auto phrases = hs.process_words({
        make_word("cat", 0),
        make_word("dog", 3),
        make_word("far", 50),
    });
    ASSERT_EQ(phrases.size(), 2u);
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"cat", "dog"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0}));
    EXPECT_EQ(phrases[1].start_offset, 50u);
    EXPECT_EQ(phrases[1].words, (std::vector<std::string>{"far"}));
    EXPECT_TRUE(phrases[1].gap_sizes.empty());
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
    EXPECT_TRUE(j["phrases"][0].contains("gap_sizes"));
    EXPECT_EQ(j["phrases"][0]["start_offset"].get<std::size_t>(), 0u);
    EXPECT_EQ(j["phrases"][0]["words"].get<std::vector<std::string>>(),
              (std::vector<std::string>{"cat", "dog"}));
}

TEST(HumanReviewScanner, IsolatedWordTextOutput) {
    HumanReviewScanner hs;
    auto phrases = hs.process_words({make_word("fox", 5)});
    std::ostringstream oss;
    hs.write_text(phrases, oss);
    EXPECT_EQ(oss.str(), "Offset 5: fox\n");
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
    EXPECT_TRUE(j["phrases"][0]["gap_sizes"].empty());
}

TEST(HumanReviewScanner, TextOutputFormat) {
    HumanReviewScanner hs;
    // emt(3) at 97 ends at 100; ten(3) at 104 gap=4
    auto phrases = hs.process_words({
        make_word("emt", 97),
        make_word("ten", 104),
    });
    std::ostringstream oss;
    hs.write_text(phrases, oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("97"), std::string::npos);
    EXPECT_NE(s.find("emt"), std::string::npos);
    EXPECT_NE(s.find("ten"), std::string::npos);
}
