#include <gtest/gtest.h>
#include "result_analyzer/ResultAnalyzer.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace result_analyzer;

namespace {

Phrase make_phrase(std::size_t offset,
                   std::vector<std::string> words,
                   std::vector<int> gaps = {}) {
    return {offset, std::move(words), std::move(gaps)};
}

AnalysisData make_data(std::vector<Phrase> phrases) {
    return {std::move(phrases)};
}

}  // namespace

// ── Cycles 1-4: parse_results_json ───────────────────────────────────────────

TEST(ResultAnalyzer, ParseEmptyPhraseArray) {
    auto data = parse_results_json(R"({"phrases":[]})");
    EXPECT_TRUE(data.phrases.empty());
}

TEST(ResultAnalyzer, ParseSinglePhrase) {
    auto data = parse_results_json(R"({
        "phrases": [{
            "start_offset": 113,
            "words": ["meh", "tut", "ewer"],
            "gap_sizes": [5, 2]
        }]
    })");
    ASSERT_EQ(data.phrases.size(), 1u);
    EXPECT_EQ(data.phrases[0].start_offset, 113u);
    EXPECT_EQ(data.phrases[0].words, (std::vector<std::string>{"meh", "tut", "ewer"}));
    EXPECT_EQ(data.phrases[0].gap_sizes, (std::vector<int>{5, 2}));
}

TEST(ResultAnalyzer, ParseMultiplePhrases) {
    auto data = parse_results_json(R"({
        "phrases": [
            {"start_offset": 5,   "words": ["a"],      "gap_sizes": []},
            {"start_offset": 100, "words": ["be", "ox"], "gap_sizes": [1]},
            {"start_offset": 200, "words": ["cat"],    "gap_sizes": []}
        ]
    })");
    ASSERT_EQ(data.phrases.size(), 3u);
    EXPECT_EQ(data.phrases[0].start_offset, 5u);
    EXPECT_EQ(data.phrases[1].start_offset, 100u);
    EXPECT_EQ(data.phrases[2].start_offset, 200u);
}

TEST(ResultAnalyzer, ParseBadJsonThrows) {
    EXPECT_THROW(parse_results_json("not json at all"),
                 nlohmann::json::exception);
}

TEST(ResultAnalyzer, ParseMissingPhrasesKeyThrows) {
    EXPECT_THROW(parse_results_json(R"({"other":[]})"),
                 std::runtime_error);
}

// ── Cycles 5-7: compute_word_occurrences ─────────────────────────────────────

TEST(ResultAnalyzer, ComputeOffsets_SingleWordPhrase) {
    auto data = make_data({make_phrase(50, {"fox"})});
    auto occ = compute_word_occurrences(data);
    ASSERT_EQ(occ.size(), 1u);
    EXPECT_EQ(occ[0].word, "fox");
    EXPECT_EQ(occ[0].offset, 50u);
}

TEST(ResultAnalyzer, ComputeOffsets_MultiWordPhrase) {
    // "meh"(3) at 113, gap=5 → "tut"(3) at 121, gap=2 → "ewer"(4) at 126
    auto data = make_data({make_phrase(113, {"meh", "tut", "ewer"}, {5, 2})});
    auto occ = compute_word_occurrences(data);
    ASSERT_EQ(occ.size(), 3u);
    EXPECT_EQ(occ[0].word, "meh");  EXPECT_EQ(occ[0].offset, 113u);
    EXPECT_EQ(occ[1].word, "tut");  EXPECT_EQ(occ[1].offset, 121u);
    EXPECT_EQ(occ[2].word, "ewer"); EXPECT_EQ(occ[2].offset, 126u);
}

TEST(ResultAnalyzer, ComputeOffsets_MultiplePhrases) {
    auto data = make_data({
        make_phrase(10, {"cat", "dog"}, {1}),
        make_phrase(500, {"ox"}),
    });
    auto occ = compute_word_occurrences(data);
    ASSERT_EQ(occ.size(), 3u);
    // "cat" at 10, "dog" at 10+3+1=14, "ox" at 500 (independent reset)
    EXPECT_EQ(occ[0].offset, 10u);
    EXPECT_EQ(occ[1].offset, 14u);
    EXPECT_EQ(occ[2].offset, 500u);
}

// ── Cycle 8: distinct_phrase_lengths ─────────────────────────────────────────

TEST(ResultAnalyzer, DistinctLengths_EmptyIsEmpty) {
    EXPECT_TRUE(distinct_phrase_lengths({}).empty());
}

TEST(ResultAnalyzer, DistinctLengths_MixedSizes) {
    std::vector<Phrase> phrases = {
        make_phrase(0, {"a", "b", "c"}),
        make_phrase(10, {"x"}),
        make_phrase(20, {"a", "b", "c"}),
        make_phrase(30, {"p", "q"}),
    };
    auto lengths = distinct_phrase_lengths(phrases);
    EXPECT_EQ(lengths, (std::vector<std::size_t>{1, 2, 3}));
}

// ── Cycles 9-11: write_phrase_file ───────────────────────────────────────────

TEST(ResultAnalyzer, WritePhraseFile_NoMatchingLength) {
    std::vector<Phrase> phrases = {make_phrase(0, {"a", "b"})};
    std::ostringstream oss;
    write_phrase_file(phrases, 3, oss);
    EXPECT_TRUE(oss.str().empty());
}

TEST(ResultAnalyzer, WritePhraseFile_SinglePhrase) {
    std::vector<Phrase> phrases = {make_phrase(113, {"meh", "tut", "ewer"}, {5, 2})};
    std::ostringstream oss;
    write_phrase_file(phrases, 3, oss);
    EXPECT_EQ(oss.str(), "113: meh tut ewer\n");
}

TEST(ResultAnalyzer, WritePhraseFile_MultiplePhrasesInOrder) {
    std::vector<Phrase> phrases = {
        make_phrase(5,   {"ox", "cat"}, {0}),
        make_phrase(100, {"be", "in"},  {2}),
        make_phrase(200, {"go", "on"},  {1}),
    };
    std::ostringstream oss;
    write_phrase_file(phrases, 2, oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("5: ox cat\n"),   std::string::npos);
    EXPECT_NE(s.find("100: be in\n"),  std::string::npos);
    EXPECT_NE(s.find("200: go on\n"),  std::string::npos);
    // order check: 5 before 100 before 200
    EXPECT_LT(s.find("5: ox"), s.find("100: be"));
    EXPECT_LT(s.find("100: be"), s.find("200: go"));
}

TEST(ResultAnalyzer, WritePhraseFile_IgnoresOtherLengths) {
    std::vector<Phrase> phrases = {
        make_phrase(10, {"cat"}),
        make_phrase(20, {"ox", "be"}, {1}),
        make_phrase(30, {"fox"}),
    };
    std::ostringstream oss;
    write_phrase_file(phrases, 1, oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("10: cat\n"), std::string::npos);
    EXPECT_NE(s.find("30: fox\n"), std::string::npos);
    EXPECT_EQ(s.find("20: ox"), std::string::npos);
}

// ── Cycles 12-15: top_n_longest_words ────────────────────────────────────────

TEST(ResultAnalyzer, TopNWords_SortsByLengthDesc) {
    std::vector<WordOccurrence> occ = {
        {"cat", 0}, {"whale", 10}, {"ox", 20}
    };
    auto top = top_n_longest_words(occ, 3);
    ASSERT_EQ(top.size(), 3u);
    EXPECT_EQ(top[0].word, "whale");
    EXPECT_EQ(top[1].word, "cat");
    EXPECT_EQ(top[2].word, "ox");
}

TEST(ResultAnalyzer, TopNWords_TieBreakerByOffsetAscending) {
    // "cat" at three offsets — deduped to one entry with the earliest offset
    std::vector<WordOccurrence> occ = {
        {"cat", 50}, {"cat", 10}, {"cat", 30}
    };
    auto top = top_n_longest_words(occ, 3);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0].word, "cat");
    EXPECT_EQ(top[0].offset, 10u);
}

TEST(ResultAnalyzer, TopNWords_FewerThanNReturnsAll) {
    std::vector<WordOccurrence> occ = {{"a", 0}, {"be", 1}, {"cat", 2}};
    auto top = top_n_longest_words(occ, 10);
    EXPECT_EQ(top.size(), 3u);
}

TEST(ResultAnalyzer, TopNWords_ExactlyNReturnsN) {
    std::vector<WordOccurrence> occ = {
        {"word", 0}, {"form", 1}, {"back", 2}, {"time", 3}, {"work", 4},
        {"hand", 5}, {"part", 6}, {"from", 7}, {"here", 8}, {"they", 9},
        {"more", 10}
    };
    auto top = top_n_longest_words(occ, 10);
    EXPECT_EQ(top.size(), 10u);
}

// ── Cycles 16-18: write_statistics ───────────────────────────────────────────

TEST(ResultAnalyzer, WriteStatistics_PhraseCounts) {
    auto data = make_data({
        make_phrase(0,   {"a"}),
        make_phrase(10,  {"be"}),
        make_phrase(20,  {"cat", "dog"}, {0}),
        make_phrase(30,  {"cat", "dog"}, {0}),
        make_phrase(40,  {"cat", "dog"}, {0}),
        make_phrase(50,  {"fox", "ox", "be"}, {1, 0}),
    });
    std::ostringstream oss;
    write_statistics(data, oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("length 1: 2"), std::string::npos);
    EXPECT_NE(s.find("length 2: 3"), std::string::npos);
    EXPECT_NE(s.find("length 3: 1"), std::string::npos);
}

TEST(ResultAnalyzer, WriteStatistics_TopTenWords) {
    // "flagstaff" (9 chars) should appear as longest word
    auto data = make_data({
        make_phrase(0, {"flagstaff"}),
        make_phrase(20, {"cat"}),
    });
    std::ostringstream oss;
    write_statistics(data, oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("flagstaff"), std::string::npos);
    EXPECT_NE(s.find("0"),         std::string::npos);
}

TEST(ResultAnalyzer, WriteStatistics_FewWordsNoTruncation) {
    auto data = make_data({
        make_phrase(0, {"a"}),
        make_phrase(5, {"be"}),
        make_phrase(10, {"cat"}),
    });
    std::ostringstream oss;
    EXPECT_NO_THROW(write_statistics(data, oss));
    // All three words appear, not truncated to 10 (there are only 3)
    const auto s = oss.str();
    EXPECT_NE(s.find("a "), std::string::npos);
}

// ── Cycles 19-20: analyze() integration ──────────────────────────────────────

namespace {

struct AnalyzeFixture {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "pi_analyze_test";

    AnalyzeFixture() { std::filesystem::create_directories(dir); }
    ~AnalyzeFixture() { std::filesystem::remove_all(dir); }

    void write_results_json(const std::string& json) {
        std::ofstream f(dir / "results.json");
        f << json;
    }
};

}  // namespace

TEST(ResultAnalyzer, Analyze_ProducesExpectedFiles) {
    AnalyzeFixture fix;
    fix.write_results_json(R"({
        "phrases": [
            {"start_offset": 5,  "words": ["ox", "cat"], "gap_sizes": [1]},
            {"start_offset": 50, "words": ["be", "in"],  "gap_sizes": [0]},
            {"start_offset": 100,"words": ["fox", "ox", "be"], "gap_sizes": [2, 1]}
        ]
    })");

    analyze(fix.dir);

    EXPECT_TRUE(std::filesystem::exists(fix.dir / "phrases_length_2.txt"));
    EXPECT_TRUE(std::filesystem::exists(fix.dir / "phrases_length_3.txt"));
    EXPECT_FALSE(std::filesystem::exists(fix.dir / "phrases_length_1.txt"));
    EXPECT_TRUE(std::filesystem::exists(fix.dir / "statistics.txt"));

    // 2-word file has 2 lines
    std::ifstream f2(fix.dir / "phrases_length_2.txt");
    int lines = 0;
    std::string line;
    while (std::getline(f2, line)) ++lines;
    EXPECT_EQ(lines, 2);
}

TEST(ResultAnalyzer, Analyze_MissingJsonThrows) {
    AnalyzeFixture fix;
    // No results.json written — directory exists but file does not
    EXPECT_THROW(analyze(fix.dir), std::runtime_error);
}
