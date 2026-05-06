#include <gtest/gtest.h>
#include "word_finder/AhoCorasickCPU.hpp"
#include <filesystem>
#include <fstream>

namespace {

std::vector<WordMatch> do_scan(AhoCorasickCPU& ac, const std::string& input,
                               std::size_t offset = 0) {
    return ac.scan(input.data(), input.size(), offset);
}

}  // namespace

TEST(AhoCorasickCPU, EmptyDictionaryNoMatches) {
    AhoCorasickCPU ac;
    ac.build();
    auto r = do_scan(ac, "helloworld");
    EXPECT_TRUE(r.empty());
}

TEST(AhoCorasickCPU, SingleWordFound) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();
    auto r = do_scan(ac, "xycatzz");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].word, "cat");
    EXPECT_EQ(r[0].start, 2u);
    EXPECT_EQ(r[0].length, 3u);
}

TEST(AhoCorasickCPU, MultipleNonOverlappingWords) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.insert_word("dog");
    ac.build();
    auto r = do_scan(ac, "catXdog");
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].word, "cat");
    EXPECT_EQ(r[0].start, 0u);
    EXPECT_EQ(r[1].word, "dog");
    EXPECT_EQ(r[1].start, 4u);
}

TEST(AhoCorasickCPU, EarliestThenLongest) {
    AhoCorasickCPU ac;
    for (auto w : {"sword", "swords", "word", "words"}) ac.insert_word(w);
    ac.build();
    auto r = do_scan(ac, "qbswordslp");
    // earliest start at pos 2: "swords" (len 6) wins over "sword" (len 5)
    // "word"/"words" at pos 3 are skipped as pos 3 < pos 2+6=8
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].word, "swords");
    EXPECT_EQ(r[0].start, 2u);
    EXPECT_EQ(r[0].length, 6u);
}

TEST(AhoCorasickCPU, MinWordLengthFiltersShortWords) {
    AhoCorasickCPU ac;
    ac.insert_word("ab");   // too short, silently dropped
    ac.insert_word("abc");
    ac.build();
    auto r = do_scan(ac, "xabc");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].word, "abc");
    EXPECT_EQ(r[0].start, 1u);
}

TEST(AhoCorasickCPU, ConsecutiveFlagTrueWhenAdjacent) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.insert_word("dog");
    ac.build();
    auto r = do_scan(ac, "catdog");
    ASSERT_EQ(r.size(), 2u);
    EXPECT_FALSE(r[0].consecutive);  // first match has no predecessor
    EXPECT_TRUE(r[1].consecutive);   // dog starts exactly where cat ended
}

TEST(AhoCorasickCPU, ConsecutiveFlagFalseWithGap) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.insert_word("dog");
    ac.build();
    auto r = do_scan(ac, "catXdog");
    ASSERT_EQ(r.size(), 2u);
    EXPECT_FALSE(r[0].consecutive);
    EXPECT_FALSE(r[1].consecutive);
}

TEST(AhoCorasickCPU, OffsetAppliedToMatchStart) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();
    auto r = do_scan(ac, "xcat", 100);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].start, 101u);  // global offset 100 + local pos 1
}

#ifdef DICT_PATH
TEST(AhoCorasickCPU, LoadDictionaryFile) {
    if (!std::filesystem::exists(DICT_PATH))
        GTEST_SKIP() << "dictionaries/english.txt absent";
    AhoCorasickCPU ac;
    ac.load_dictionary(DICT_PATH);
    ac.build();
    auto r = do_scan(ac, "qbswordslp");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].word, "swords");
}
#endif
