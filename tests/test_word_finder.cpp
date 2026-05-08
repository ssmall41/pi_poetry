#include <gtest/gtest.h>
#include "word_finder/AhoCorasickCPU.hpp"
#include <filesystem>
#include <fstream>

namespace {

std::vector<std::vector<WordMatch>> do_scan(AhoCorasickCPU& ac,
                                             const std::string& input,
                                             std::size_t offset = 0) {
    return ac.scan(input.data(), input.size(), offset);
}

}  // namespace

TEST(AhoCorasickCPU, EmptyDictionaryNoMatches) {
    AhoCorasickCPU ac;
    ac.build();
    auto r = do_scan(ac, "helloworld");
    // ETL with no matches: one empty sequence
    ASSERT_EQ(r.size(), 1u);
    EXPECT_TRUE(r[0].empty());
}

TEST(AhoCorasickCPU, SingleWordFound) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();
    auto r = do_scan(ac, "xycatzz");
    ASSERT_EQ(r[0].size(), 1u);
    EXPECT_EQ(r[0][0].word, "cat");
    EXPECT_EQ(r[0][0].start, 2u);
    EXPECT_EQ(r[0][0].length, 3u);
}

TEST(AhoCorasickCPU, MultipleNonOverlappingWords) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.insert_word("dog");
    ac.build();
    auto r = do_scan(ac, "catXdog");
    ASSERT_EQ(r[0].size(), 2u);
    EXPECT_EQ(r[0][0].word, "cat");
    EXPECT_EQ(r[0][0].start, 0u);
    EXPECT_EQ(r[0][1].word, "dog");
    EXPECT_EQ(r[0][1].start, 4u);
}

TEST(AhoCorasickCPU, EarliestThenLongest) {
    AhoCorasickCPU ac;
    for (auto w : {"sword", "swords", "word", "words"}) ac.insert_word(w);
    ac.build();
    auto r = do_scan(ac, "qbswordslp");
    // earliest start at pos 2: "swords" (len 6) wins over "sword" (len 5)
    // "word"/"words" at pos 3 are skipped as pos 3 < pos 2+6=8
    ASSERT_EQ(r[0].size(), 1u);
    EXPECT_EQ(r[0][0].word, "swords");
    EXPECT_EQ(r[0][0].start, 2u);
    EXPECT_EQ(r[0][0].length, 6u);
}

TEST(AhoCorasickCPU, MinWordLengthFiltersShortWords) {
    AhoCorasickCPU ac;
    ac.set_min_word_length(3);
    ac.insert_word("ab");   // too short, silently dropped
    ac.insert_word("abc");
    ac.build();
    auto r = do_scan(ac, "xabc");
    ASSERT_EQ(r[0].size(), 1u);
    EXPECT_EQ(r[0][0].word, "abc");
    EXPECT_EQ(r[0][0].start, 1u);
}

TEST(AhoCorasickCPU, MinWordLength1AcceptsSingleLetterWords) {
    AhoCorasickCPU ac;
    ac.set_min_word_length(1);
    ac.insert_word("a");
    ac.insert_word("cat");
    ac.build();
    auto r = do_scan(ac, "xa");
    ASSERT_EQ(r[0].size(), 1u);
    EXPECT_EQ(r[0][0].word, "a");
    EXPECT_EQ(r[0][0].start, 1u);
}

TEST(AhoCorasickCPU, MinWordLength4RejectsThreeLetterWords) {
    AhoCorasickCPU ac;
    ac.set_min_word_length(4);
    ac.insert_word("cat");   // 3 letters, dropped
    ac.insert_word("cats");  // 4 letters, accepted
    ac.build();
    auto r = do_scan(ac, "xcats");
    ASSERT_EQ(r[0].size(), 1u);
    EXPECT_EQ(r[0][0].word, "cats");
    EXPECT_EQ(r[0][0].start, 1u);
}

TEST(AhoCorasickCPU, ConsecutiveFlagTrueWhenAdjacent) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.insert_word("dog");
    ac.build();
    auto r = do_scan(ac, "catdog");
    ASSERT_EQ(r[0].size(), 2u);
    EXPECT_FALSE(r[0][0].consecutive);  // first match has no predecessor
    EXPECT_TRUE(r[0][1].consecutive);   // dog starts exactly where cat ended
}

TEST(AhoCorasickCPU, ConsecutiveFlagFalseWithGap) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.insert_word("dog");
    ac.build();
    auto r = do_scan(ac, "catXdog");
    ASSERT_EQ(r[0].size(), 2u);
    EXPECT_FALSE(r[0][0].consecutive);
    EXPECT_FALSE(r[0][1].consecutive);
}

TEST(AhoCorasickCPU, OffsetAppliedToMatchStart) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();
    auto r = do_scan(ac, "xcat", 100);
    ASSERT_EQ(r[0].size(), 1u);
    EXPECT_EQ(r[0][0].start, 101u);  // global offset 100 + local pos 1
}

#ifdef DICT_PATH
TEST(AhoCorasickCPU, LoadDictionaryFile) {
    if (!std::filesystem::exists(DICT_PATH))
        GTEST_SKIP() << "dictionaries/english.txt absent";
    AhoCorasickCPU ac;
    ac.set_min_word_length(3);
    ac.load_dictionary(DICT_PATH);
    ac.build();
    auto r = do_scan(ac, "qbswordslp");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0][0].word, "swords");
}
#endif

// ── AllCombos policy tests ──────────────────────────────────────────────────

TEST(AhoCorasickCPU, AllCombos_TwoAlternatives) {
    // "ab" with {a, ab, b}: ETL picks [ab]; AllCombos finds [ab] and [a,b]
    AhoCorasickCPU ac;
    for (auto w : {"a", "ab", "b"}) ac.insert_word(w);
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);
    auto seqs = ac.scan("ab", 2, 0);

    auto has_seq = [&](std::vector<std::string> words) {
        return std::any_of(seqs.begin(), seqs.end(), [&](const auto& seq) {
            if (seq.size() != words.size()) return false;
            for (std::size_t i = 0; i < words.size(); ++i)
                if (seq[i].word != words[i]) return false;
            return true;
        });
    };

    EXPECT_TRUE(has_seq({"ab"}));
    EXPECT_TRUE(has_seq({"a", "b"}));
    EXPECT_TRUE(has_seq({"b"}));   // "b" at pos 1 is its own valid start
}

TEST(AhoCorasickCPU, AllCombos_ConsecutiveFlag) {
    AhoCorasickCPU ac;
    for (auto w : {"a", "ab", "b"}) ac.insert_word(w);
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);
    auto seqs = ac.scan("ab", 2, 0);

    auto find_seq = [&](std::vector<std::string> words) -> const std::vector<WordMatch>* {
        for (const auto& seq : seqs) {
            if (seq.size() != words.size()) continue;
            bool match = true;
            for (std::size_t i = 0; i < words.size(); ++i)
                if (seq[i].word != words[i]) { match = false; break; }
            if (match) return &seq;
        }
        return nullptr;
    };

    auto* two = find_seq({"a", "b"});
    ASSERT_NE(two, nullptr);
    EXPECT_FALSE((*two)[0].consecutive);
    EXPECT_TRUE((*two)[1].consecutive);

    auto* one = find_seq({"ab"});
    ASSERT_NE(one, nullptr);
    EXPECT_FALSE((*one)[0].consecutive);
}

TEST(AhoCorasickCPU, AllCombos_EmptyInput) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);
    auto seqs = ac.scan("", 0, 0);
    EXPECT_TRUE(seqs.empty());
}

TEST(AhoCorasickCPU, AllCombos_NoMatches) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);
    auto seqs = ac.scan("xyz", 3, 0);
    EXPECT_TRUE(seqs.empty());
}

TEST(AhoCorasickCPU, AllCombos_OffsetApplied) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);
    auto seqs = ac.scan("cat", 3, 100);
    ASSERT_EQ(seqs.size(), 1u);
    ASSERT_EQ(seqs[0].size(), 1u);
    EXPECT_EQ(seqs[0][0].start, 100u);
}

TEST(AhoCorasickCPU, AllCombos_Swords) {
    AhoCorasickCPU ac;
    for (auto w : {"s", "sword", "swords", "word", "words"}) ac.insert_word(w);
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);
    auto seqs = ac.scan("swords", 6, 0);

    auto has_seq = [&](std::vector<std::string> words) {
        return std::any_of(seqs.begin(), seqs.end(), [&](const auto& seq) {
            if (seq.size() != words.size()) return false;
            for (std::size_t i = 0; i < words.size(); ++i)
                if (seq[i].word != words[i]) return false;
            return true;
        });
    };

    // Sequences starting at pos 0
    EXPECT_TRUE(has_seq({"swords"}));
    EXPECT_TRUE(has_seq({"sword"}));
    EXPECT_TRUE(has_seq({"s", "words"}));
    EXPECT_TRUE(has_seq({"s", "word"}));
    // Sequences starting at pos 1
    EXPECT_TRUE(has_seq({"words"}));
    EXPECT_TRUE(has_seq({"word"}));
}

#ifdef DICT_PATH
TEST(AhoCorasickCPU, AllCombos_AwhaleOfATale) {
    if (!std::filesystem::exists(DICT_PATH))
        GTEST_SKIP() << "dictionaries/english.txt absent";
    AhoCorasickCPU ac;
    ac.set_min_word_length(1);
    ac.load_dictionary(DICT_PATH);
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);

    const std::string letters = "awhaleofatale";
    auto seqs = ac.scan(letters.data(), letters.size(), 0);

    auto has_seq = [&](std::vector<std::string> words) {
        return std::any_of(seqs.begin(), seqs.end(), [&](const auto& seq) {
            if (seq.size() != words.size()) return false;
            for (std::size_t i = 0; i < words.size(); ++i)
                if (seq[i].word != words[i]) return false;
            return true;
        });
    };

    EXPECT_TRUE(has_seq({"a", "whale", "of", "a", "tale"}));
    EXPECT_TRUE(has_seq({"aw", "hale", "of", "at", "ale"}));
    EXPECT_TRUE(has_seq({"a", "whale", "of", "at", "ale"}));
    EXPECT_TRUE(has_seq({"aw", "hale", "of", "a", "tale"}));
}
#endif
