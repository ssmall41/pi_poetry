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
    // ETL with no matches: empty outer vector (symmetric with AllCombos)
    EXPECT_TRUE(r.empty());
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

// ── scan_chunk: stateful incremental scanning ──────────────────────────────

using RawVec = std::vector<std::pair<std::size_t, std::string>>;

TEST(AhoCorasickCPU, ScanChunkStateful) {
    // Scan "catdog" as two chunks; raw output must match a single full scan.
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.insert_word("dog");
    ac.build();

    int state = 0;
    RawVec raw;
    ac.scan_chunk("cat", 3, 0, state, raw);  // global_offset=0
    ac.scan_chunk("dog", 3, 3, state, raw);  // global_offset=3

    ASSERT_EQ(raw.size(), 2u);
    EXPECT_EQ(raw[0].first, 0u);     // "cat" starts at global pos 0
    EXPECT_EQ(raw[0].second, "cat");
    EXPECT_EQ(raw[1].first, 3u);     // "dog" starts at global pos 3
    EXPECT_EQ(raw[1].second, "dog");
}

TEST(AhoCorasickCPU, ScanChunkBoundaryWord) {
    // "and" spans the chunk boundary: chunk1="xa", chunk2="ndx".
    // State must carry over so the word is found.
    AhoCorasickCPU ac;
    ac.insert_word("and");
    ac.build();

    int state = 0;
    RawVec raw;
    ac.scan_chunk("xa", 2, 0, state, raw);   // offsets 0-1
    ac.scan_chunk("ndx", 3, 2, state, raw);  // offsets 2-4

    ASSERT_EQ(raw.size(), 1u);
    EXPECT_EQ(raw[0].second, "and");
    EXPECT_EQ(raw[0].first, 1u);  // "and" starts at global position 1
}

TEST(AhoCorasickCPU, ScanChunkNonLetterResetsState) {
    // A non-letter character in a chunk must reset the AC state to root.
    AhoCorasickCPU ac;
    ac.insert_word("and");
    ac.build();

    int state = 0;
    RawVec raw;
    // 'a' advances state, then '1' (non-letter) resets to root,
    // so "nd" in the next chunk cannot complete "and".
    ac.scan_chunk("a1", 2, 0, state, raw);
    ac.scan_chunk("nd", 2, 2, state, raw);

    EXPECT_TRUE(raw.empty());
}

TEST(AhoCorasickCPU, EtlCallbackEquivalence) {
    // apply_etl_cb must emit the same chain as scan() with ETL policy.
    AhoCorasickCPU ac;
    for (auto w : {"a", "ab", "b"}) ac.insert_word(w);
    ac.build();
    // ETL is default policy; no set_overlap_policy needed

    // Collect via return-value scan()
    auto seqs = ac.scan("ab", 2, 0);

    // Collect via scan_chunk + apply_etl_cb
    int state = 0;
    RawVec raw;
    ac.scan_chunk("ab", 2, 0, state, raw);

    std::vector<std::vector<WordMatch>> cb_chains;
    ac.apply_etl_cb(raw, 0,
        [&](const std::vector<WordMatch>& chain) { cb_chains.push_back(chain); });

    auto to_word_lists = [](const std::vector<std::vector<WordMatch>>& chains) {
        std::vector<std::vector<std::string>> result;
        for (const auto& chain : chains) {
            std::vector<std::string> words;
            for (const auto& m : chain) words.push_back(m.word);
            result.push_back(words);
        }
        std::sort(result.begin(), result.end());
        return result;
    };

    ASSERT_EQ(seqs.size(), cb_chains.size());
    EXPECT_EQ(to_word_lists(seqs), to_word_lists(cb_chains));
}

TEST(AhoCorasickCPU, EtlCallbackNoMatchesCallsOnChainZeroTimes) {
    AhoCorasickCPU ac;
    ac.insert_word("cat");
    ac.build();

    int state = 0;
    RawVec raw;
    ac.scan_chunk("xyz", 3, 0, state, raw);

    int call_count = 0;
    ac.apply_etl_cb(raw, 0,
        [&](const std::vector<WordMatch>&) { ++call_count; });

    EXPECT_EQ(call_count, 0);
}

TEST(AhoCorasickCPU, EtlIsSubsetOfAllCombos) {
    AhoCorasickCPU ac;
    for (auto w : {"s", "sword", "swords", "word", "words"}) ac.insert_word(w);
    ac.build();

    int state = 0;
    RawVec raw;
    ac.scan_chunk("swords", 6, 0, state, raw);

    // Collect ETL chains (at most 1)
    std::vector<std::vector<std::string>> etl_word_lists;
    ac.apply_etl_cb(raw, 0, [&](const std::vector<WordMatch>& chain) {
        std::vector<std::string> words;
        for (const auto& m : chain) words.push_back(m.word);
        etl_word_lists.push_back(words);
    });

    // Collect AllCombos chains
    std::vector<std::vector<std::string>> ac_word_lists;
    ac.apply_all_combos_cb(raw, 0, [&](const std::vector<WordMatch>& chain) {
        std::vector<std::string> words;
        for (const auto& m : chain) words.push_back(m.word);
        ac_word_lists.push_back(words);
    });

    // Every ETL chain must appear in AllCombos
    for (const auto& etl_chain : etl_word_lists) {
        EXPECT_TRUE(std::find(ac_word_lists.begin(), ac_word_lists.end(), etl_chain) != ac_word_lists.end())
            << "ETL chain not found in AllCombos output";
    }
}

TEST(AhoCorasickCPU, AllCombosCallbackEquivalence) {
    // apply_all_combos_cb must emit exactly the same chains as scan() with AllCombos.
    AhoCorasickCPU ac;
    for (auto w : {"a", "ab", "b"}) ac.insert_word(w);
    ac.build();
    ac.set_overlap_policy(OverlapPolicy::AllCombos);

    // Collect via existing return-value scan()
    auto seqs = ac.scan("ab", 2, 0);

    // Collect via scan_chunk + apply_all_combos_cb
    int state = 0;
    RawVec raw;
    ac.scan_chunk("ab", 2, 0, state, raw);

    std::vector<std::vector<WordMatch>> cb_chains;
    ac.apply_all_combos_cb(raw, 0,
        [&](const std::vector<WordMatch>& chain) { cb_chains.push_back(chain); });

    // Extract word-lists for comparison (order may differ between the two APIs)
    auto to_word_lists = [](const std::vector<std::vector<WordMatch>>& chains) {
        std::vector<std::vector<std::string>> result;
        for (const auto& chain : chains) {
            std::vector<std::string> words;
            for (const auto& m : chain) words.push_back(m.word);
            result.push_back(words);
        }
        std::sort(result.begin(), result.end());
        return result;
    };

    ASSERT_EQ(seqs.size(), cb_chains.size());
    EXPECT_EQ(to_word_lists(seqs), to_word_lists(cb_chains));
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
