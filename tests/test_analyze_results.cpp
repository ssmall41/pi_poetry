#include <gtest/gtest.h>
#include "result_analyzer/ResultAnalyzer.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace result_analyzer;

// ── analyze() integration ────────────────────────────────────────────────────

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

TEST(ResultAnalyzer, Analyze_MissingPhrasesKeyThrows) {
    AnalyzeFixture fix;
    fix.write_results_json(R"({"other":[]})");
    EXPECT_THROW(analyze(fix.dir), std::runtime_error);
}

TEST(ResultAnalyzer, Analyze_MalformedJsonThrows) {
    AnalyzeFixture fix;
    fix.write_results_json("not json at all");
    EXPECT_THROW(analyze(fix.dir), nlohmann::json::exception);
}

TEST(ResultAnalyzer, Analyze_StatisticsContentMatchesFixture) {
    AnalyzeFixture fix;
    fix.write_results_json(R"({
        "phrases": [
            {"start_offset": 5,  "words": ["ox", "cat"], "gap_sizes": [1]},
            {"start_offset": 50, "words": ["be", "in"],  "gap_sizes": [0]},
            {"start_offset": 100,"words": ["fox", "ox", "be"], "gap_sizes": [2, 1]}
        ]
    })");

    analyze(fix.dir);

    std::ifstream stats(fix.dir / "statistics.txt");
    std::ostringstream oss;
    oss << stats.rdbuf();
    const auto s = oss.str();

    EXPECT_NE(s.find("length 2: 2"), std::string::npos);
    EXPECT_NE(s.find("length 3: 1"), std::string::npos);
    // "fox" (3 chars) at offset 100 is the longest word
    EXPECT_NE(s.find("fox at offset 100 (length 3)"), std::string::npos);
}

// ── PhraseFileWriter ──────────────────────────────────────────────────────────

namespace {

struct WriterFixture {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "pi_phrase_file_writer_test";

    WriterFixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~WriterFixture() { std::filesystem::remove_all(dir); }

    std::string read(const std::string& filename) {
        std::ifstream f(dir / filename);
        std::ostringstream oss;
        oss << f.rdbuf();
        return oss.str();
    }
};

}  // namespace

TEST(PhraseFileWriter, WritesSinglePhraseLine) {
    WriterFixture fix;
    {
        PhraseFileWriter writer(fix.dir);
        writer.write_phrase(113, {"meh", "tut", "ewer"});
    }
    EXPECT_EQ(fix.read("phrases_length_3.txt"), "113: meh tut ewer\n");
}

TEST(PhraseFileWriter, SeparatesByLength) {
    WriterFixture fix;
    {
        PhraseFileWriter writer(fix.dir);
        writer.write_phrase(5,  {"ox", "cat"});
        writer.write_phrase(50, {"be", "in", "go"});
    }
    EXPECT_EQ(fix.read("phrases_length_2.txt"), "5: ox cat\n");
    EXPECT_EQ(fix.read("phrases_length_3.txt"), "50: be in go\n");
    EXPECT_FALSE(std::filesystem::exists(fix.dir / "phrases_length_1.txt"));
}

TEST(PhraseFileWriter, AppendsMultiplePhrasesSameLength) {
    WriterFixture fix;
    {
        PhraseFileWriter writer(fix.dir);
        writer.write_phrase(5,   {"ox", "cat"});
        writer.write_phrase(100, {"be", "in"});
        writer.write_phrase(200, {"go", "on"});
    }
    EXPECT_EQ(fix.read("phrases_length_2.txt"),
              "5: ox cat\n100: be in\n200: go on\n");
}

// ── write_statistics(PhraseStatsAccumulator) ─────────────────────────────────

TEST(WriteStatisticsAccumulator, PhraseCounts) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(0,  {"a"});
    stats.add_phrase(10, {"be"});
    stats.add_phrase(20, {"cat", "dog"});
    stats.add_phrase(30, {"cat", "dog"});
    stats.add_phrase(40, {"cat", "dog"});
    stats.add_phrase(50, {"fox", "ox", "be"});

    std::ostringstream oss;
    write_statistics(stats, oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("length 1: 2"), std::string::npos);
    EXPECT_NE(s.find("length 2: 3"), std::string::npos);
    EXPECT_NE(s.find("length 3: 1"), std::string::npos);
}

TEST(WriteStatisticsAccumulator, TopTenWords) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(0,  {"flagstaff"});
    stats.add_phrase(20, {"cat"});

    std::ostringstream oss;
    write_statistics(stats, oss);
    const auto s = oss.str();
    EXPECT_NE(s.find("flagstaff"), std::string::npos);
    EXPECT_NE(s.find("0"), std::string::npos);
}

TEST(WriteStatisticsAccumulator, FewWordsNoTruncation) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(0,  {"a"});
    stats.add_phrase(5,  {"be"});
    stats.add_phrase(10, {"cat"});

    std::ostringstream oss;
    EXPECT_NO_THROW(write_statistics(stats, oss));
    const auto s = oss.str();
    EXPECT_NE(s.find("a "), std::string::npos);
}

// ── PhraseStreamHandler ───────────────────────────────────────────────────────

TEST(PhraseStreamHandler, EmptyPhrasesArray) {
    std::vector<ParsedPhrase> phrases;
    PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });

    EXPECT_TRUE(nlohmann::json::sax_parse(R"({"phrases":[]})", &handler));
    EXPECT_TRUE(phrases.empty());
    EXPECT_TRUE(handler.saw_phrases_array());
}

TEST(PhraseStreamHandler, SinglePhrase) {
    std::vector<ParsedPhrase> phrases;
    PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });

    EXPECT_TRUE(nlohmann::json::sax_parse(
        R"({"phrases":[{"start_offset":113,"words":["meh","tut","ewer"]}]})", &handler));

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 113u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"meh", "tut", "ewer"}));
}

TEST(PhraseStreamHandler, MultiplePhrases) {
    std::vector<ParsedPhrase> phrases;
    PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });

    EXPECT_TRUE(nlohmann::json::sax_parse(R"({
        "phrases": [
            {"start_offset": 5,   "words": ["a"]},
            {"start_offset": 100, "words": ["be", "ox"]},
            {"start_offset": 200, "words": ["cat"]}
        ]
    })", &handler));

    ASSERT_EQ(phrases.size(), 3u);
    EXPECT_EQ(phrases[0].start_offset, 5u);
    EXPECT_EQ(phrases[1].start_offset, 100u);
    EXPECT_EQ(phrases[2].start_offset, 200u);
}

TEST(PhraseStreamHandler, IgnoresExtraKeys) {
    std::vector<ParsedPhrase> phrases;
    PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });

    EXPECT_TRUE(nlohmann::json::sax_parse(
        R"({"phrases":[{"start_offset":5,"words":["ox","cat"],"gap_sizes":[1]}]})", &handler));

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 5u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"ox", "cat"}));
}

TEST(PhraseStreamHandler, NoPhrasesKey) {
    std::vector<ParsedPhrase> phrases;
    PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });

    EXPECT_TRUE(nlohmann::json::sax_parse(R"({"other":[]})", &handler));
    EXPECT_FALSE(handler.saw_phrases_array());
}

TEST(PhraseStreamHandler, MalformedJsonThrows) {
    std::vector<ParsedPhrase> phrases;
    PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });

    EXPECT_THROW(nlohmann::json::sax_parse("not json at all", &handler),
                 nlohmann::json::exception);
}

// ── PhraseStatsAccumulator ───────────────────────────────────────────────────

TEST(PhraseStatsAccumulator, EmptyHasNoLengthCounts) {
    PhraseStatsAccumulator stats;
    EXPECT_TRUE(stats.length_counts().empty());
}

TEST(PhraseStatsAccumulator, AddPhraseUpdatesLengthCounts) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(0, {"a", "b"});
    auto counts = stats.length_counts();
    ASSERT_EQ(counts.size(), 1u);
    EXPECT_EQ(counts.at(2), 1u);
}

TEST(PhraseStatsAccumulator, TopN_EmptyReturnsEmpty) {
    PhraseStatsAccumulator stats;
    EXPECT_TRUE(stats.top_n_longest_words(10).empty());
}

TEST(PhraseStatsAccumulator, TopN_SortsByLengthDesc) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(0,  {"cat"});
    stats.add_phrase(10, {"whale"});
    stats.add_phrase(20, {"ox"});

    auto top = stats.top_n_longest_words(3);
    ASSERT_EQ(top.size(), 3u);
    EXPECT_EQ(top[0].word, "whale");
    EXPECT_EQ(top[1].word, "cat");
    EXPECT_EQ(top[2].word, "ox");
}

TEST(PhraseStatsAccumulator, TopN_DedupKeepsMinOffset) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(50, {"cat"});
    stats.add_phrase(10, {"cat"});
    stats.add_phrase(30, {"cat"});

    auto top = stats.top_n_longest_words(3);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0].word, "cat");
    EXPECT_EQ(top[0].offset, 10u);
}

TEST(PhraseStatsAccumulator, TopN_FewerThanNReturnsAll) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(0, {"a"});
    stats.add_phrase(1, {"be"});
    stats.add_phrase(2, {"cat"});

    EXPECT_EQ(stats.top_n_longest_words(10).size(), 3u);
}

TEST(PhraseStatsAccumulator, TopN_ExactlyNReturnsN) {
    PhraseStatsAccumulator stats;
    std::vector<std::string> words = {
        "word", "form", "back", "time", "work",
        "hand", "part", "from", "here", "they", "more"
    };
    for (std::size_t i = 0; i < words.size(); ++i)
        stats.add_phrase(i, {words[i]});

    auto top = stats.top_n_longest_words(10);
    ASSERT_EQ(top.size(), 10u);
    for (std::size_t i = 0; i + 1 < top.size(); ++i)
        EXPECT_LT(top[i].offset, top[i + 1].offset);
}

TEST(PhraseStatsAccumulator, AddPhrase_ComputesCumulativeOffsets) {
    PhraseStatsAccumulator stats;
    // "meh"(3) at 113 -> "tut"(3) at 116 -> "ewer"(4) at 119
    stats.add_phrase(113, {"meh", "tut", "ewer"});
    // A longer dummy word so "ewer" still shows up within top-5.
    stats.add_phrase(0, {"flagstaff"});

    auto top = stats.top_n_longest_words(5);
    auto it = std::find_if(top.begin(), top.end(),
                            [](const WordOccurrence& wo) { return wo.word == "ewer"; });
    ASSERT_NE(it, top.end());
    EXPECT_EQ(it->offset, 119u);
}

TEST(PhraseStatsAccumulator, MultiplePhrasesAccumulateCounts) {
    PhraseStatsAccumulator stats;
    stats.add_phrase(0,  {"a"});
    stats.add_phrase(10, {"be"});
    stats.add_phrase(20, {"cat", "dog"});
    stats.add_phrase(30, {"cat", "dog"});
    stats.add_phrase(40, {"cat", "dog"});
    stats.add_phrase(50, {"fox", "ox", "be"});

    auto counts = stats.length_counts();
    EXPECT_EQ(counts.at(1), 2u);
    EXPECT_EQ(counts.at(2), 3u);
    EXPECT_EQ(counts.at(3), 1u);
}
