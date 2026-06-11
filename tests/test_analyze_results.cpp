#include <gtest/gtest.h>
#include "result_analyzer/ResultAnalyzerInternal.hpp"  // ResultAnalyzer.hpp + PhraseStreamHandler
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

TEST(PhraseStatsAccumulator, Merge_SumsLengthCounts) {
    PhraseStatsAccumulator a;
    a.add_phrase(0, {"a"});
    a.add_phrase(10, {"cat", "dog"});

    PhraseStatsAccumulator b;
    b.add_phrase(20, {"be"});
    b.add_phrase(30, {"fox", "ox"});
    b.add_phrase(40, {"win", "an", "it"});

    a.merge(b);

    auto counts = a.length_counts();
    EXPECT_EQ(counts.at(1), 2u);
    EXPECT_EQ(counts.at(2), 2u);
    EXPECT_EQ(counts.at(3), 1u);
}

TEST(PhraseStatsAccumulator, Merge_KeepsMinOffsetPerWord) {
    PhraseStatsAccumulator a;
    a.add_phrase(50, {"cat"});

    PhraseStatsAccumulator b;
    b.add_phrase(10, {"cat"});

    a.merge(b);

    auto top = a.top_n_longest_words(1);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0].word, "cat");
    EXPECT_EQ(top[0].offset, 10u);
}

// ── find_phrase_chunk_boundaries ─────────────────────────────────────────────

namespace {

struct BoundaryFixture {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "pi_boundary_test";
    std::filesystem::path json_path = dir / "results.json";

    BoundaryFixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~BoundaryFixture() { std::filesystem::remove_all(dir); }

    void write(const std::string& content) {
        std::ofstream f(json_path, std::ios::binary);
        f << content;
    }

    // Reads bytes [start, end) from json_path, wraps as {"phrases":[...]},
    // and parses the resulting phrase objects.
    std::vector<ParsedPhrase> parse_chunk(std::size_t start, std::size_t end) const {
        std::ifstream f(json_path, std::ios::binary);
        f.seekg(static_cast<std::streamoff>(start));
        std::string buf(end - start, '\0');
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));

        std::string wrapped = "{\"phrases\":[" + buf + "]}";
        std::vector<ParsedPhrase> phrases;
        PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });
        nlohmann::json::sax_parse(wrapped, &handler);
        return phrases;
    }
};

}  // namespace

TEST(FindChunkBoundaries, SinglePhraseReturnsOneChunk) {
    BoundaryFixture fix;
    fix.write(R"({"phrases":[{"start_offset":5,"words":["ab"]}]})");

    auto chunks = find_phrase_chunk_boundaries(fix.json_path, 4);
    ASSERT_EQ(chunks.size(), 1u);

    auto phrases = fix.parse_chunk(chunks[0].first, chunks[0].second);
    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 5u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"ab"}));
}

TEST(FindChunkBoundaries, SplitsEvenPhrasesAcrossThreads) {
    BoundaryFixture fix;
    fix.write(R"({"phrases":[)"
              R"({"start_offset":10,"words":["aaa"]},)"
              R"({"start_offset":20,"words":["bbb"]},)"
              R"({"start_offset":30,"words":["ccc"]},)"
              R"({"start_offset":40,"words":["ddd"]},)"
              R"({"start_offset":50,"words":["eee"]},)"
              R"({"start_offset":60,"words":["fff"]})"
              R"(]})");

    auto chunks = find_phrase_chunk_boundaries(fix.json_path, 2);
    ASSERT_EQ(chunks.size(), 2u);

    auto chunk0 = fix.parse_chunk(chunks[0].first, chunks[0].second);
    auto chunk1 = fix.parse_chunk(chunks[1].first, chunks[1].second);
    EXPECT_EQ(chunk0.size(), 3u);
    EXPECT_EQ(chunk1.size(), 3u);
}

TEST(FindChunkBoundaries, FewerPhrasesThanThreadsCapsEffectiveThreads) {
    BoundaryFixture fix;
    fix.write(R"({"phrases":[)"
              R"({"start_offset":1,"words":["a"]},)"
              R"({"start_offset":2,"words":["b"]},)"
              R"({"start_offset":3,"words":["c"]})"
              R"(]})");

    auto chunks = find_phrase_chunk_boundaries(fix.json_path, 8);
    ASSERT_EQ(chunks.size(), 3u);

    std::size_t total = 0;
    for (const auto& [start, end] : chunks)
        total += fix.parse_chunk(start, end).size();
    EXPECT_EQ(total, 3u);
}

TEST(FindChunkBoundaries, EmptyPhrasesArraySignalsFallback) {
    BoundaryFixture fix;
    fix.write(R"({"phrases":[]})");
    EXPECT_TRUE(find_phrase_chunk_boundaries(fix.json_path, 4).empty());
}

TEST(FindChunkBoundaries, MissingPhrasesKeySignalsFallback) {
    BoundaryFixture fix;
    fix.write(R"({"other":[]})");
    EXPECT_TRUE(find_phrase_chunk_boundaries(fix.json_path, 4).empty());
}

TEST(FindChunkBoundaries, HandlesNestedArraysInObjects) {
    BoundaryFixture fix;
    fix.write(R"({"phrases":[)"
              R"({"gap_sizes":[1,2],"start_offset":5,"words":["a","bb","ccc"]},)"
              R"({"gap_sizes":[],"start_offset":100,"words":["xy"]})"
              R"(]})");

    auto chunks = find_phrase_chunk_boundaries(fix.json_path, 2);
    ASSERT_EQ(chunks.size(), 2u);

    auto chunk0 = fix.parse_chunk(chunks[0].first, chunks[0].second);
    auto chunk1 = fix.parse_chunk(chunks[1].first, chunks[1].second);
    ASSERT_EQ(chunk0.size(), 1u);
    ASSERT_EQ(chunk1.size(), 1u);
    EXPECT_EQ(chunk0[0].words, (std::vector<std::string>{"a", "bb", "ccc"}));
    EXPECT_EQ(chunk1[0].words, (std::vector<std::string>{"xy"}));
}

// ── PhraseChunkStreamBuf ──────────────────────────────────────────────────────

TEST(PhraseChunkStreamBuf, WrapsRangeWithPhrasesEnvelope) {
    BoundaryFixture fix;
    fix.write(R"({"phrases":[)"
              R"({"start_offset":10,"words":["aaa"]})"
              R"(]})");

    auto chunks = find_phrase_chunk_boundaries(fix.json_path, 1);
    ASSERT_EQ(chunks.size(), 1u);
    auto [start, end] = chunks[0];

    std::ifstream raw_f(fix.json_path, std::ios::binary);
    raw_f.seekg(static_cast<std::streamoff>(start));
    std::string raw(end - start, '\0');
    raw_f.read(raw.data(), static_cast<std::streamsize>(raw.size()));

    PhraseChunkStreamBuf buf(fix.json_path, start, end);
    std::istream in(&buf);
    std::ostringstream oss;
    oss << in.rdbuf();

    EXPECT_EQ(oss.str(), "{\"phrases\":[" + raw + "]}");
}

TEST(PhraseChunkStreamBuf, FeedsSaxParserCorrectly) {
    BoundaryFixture fix;
    fix.write(R"({"phrases":[)"
              R"({"start_offset":10,"words":["aaa"]},)"
              R"({"start_offset":20,"words":["bbb"]},)"
              R"({"start_offset":30,"words":["ccc"]},)"
              R"({"start_offset":40,"words":["ddd"]},)"
              R"({"start_offset":50,"words":["eee"]},)"
              R"({"start_offset":60,"words":["fff"]})"
              R"(]})");

    auto chunks = find_phrase_chunk_boundaries(fix.json_path, 2);
    ASSERT_EQ(chunks.size(), 2u);

    PhraseChunkStreamBuf buf(fix.json_path, chunks[1].first, chunks[1].second);
    std::istream in(&buf);

    std::vector<ParsedPhrase> phrases;
    PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });
    EXPECT_TRUE(nlohmann::json::sax_parse(in, &handler));

    ASSERT_EQ(phrases.size(), 3u);
    EXPECT_EQ(phrases[0].start_offset, 40u);
    EXPECT_EQ(phrases[0].words, (std::vector<std::string>{"ddd"}));
    EXPECT_EQ(phrases[1].start_offset, 50u);
    EXPECT_EQ(phrases[2].start_offset, 60u);
    EXPECT_EQ(phrases[2].words, (std::vector<std::string>{"fff"}));
}

TEST(PhraseChunkStreamBuf, HandlesRangeLargerThanInternalBuffer) {
    BoundaryFixture fix;

    constexpr int kCount = 5000;
    std::ostringstream json;
    json << R"({"phrases":[)";
    for (int i = 0; i < kCount; ++i) {
        if (i > 0) json << ',';
        json << R"({"start_offset":)" << i << R"(,"words":["word)" << i << R"("]})";
    }
    json << "]}";
    fix.write(json.str());

    auto chunks = find_phrase_chunk_boundaries(fix.json_path, 1);
    ASSERT_EQ(chunks.size(), 1u);
    auto [start, end] = chunks[0];
    ASSERT_GT(end - start, 65536u);

    std::ifstream raw_f(fix.json_path, std::ios::binary);
    raw_f.seekg(static_cast<std::streamoff>(start));
    std::string raw(end - start, '\0');
    raw_f.read(raw.data(), static_cast<std::streamsize>(raw.size()));

    {
        PhraseChunkStreamBuf buf(fix.json_path, start, end);
        std::istream in(&buf);
        std::ostringstream oss;
        oss << in.rdbuf();
        EXPECT_EQ(oss.str(), "{\"phrases\":[" + raw + "]}");
    }

    {
        PhraseChunkStreamBuf buf(fix.json_path, start, end);
        std::istream in(&buf);
        std::vector<ParsedPhrase> phrases;
        PhraseStreamHandler handler([&](ParsedPhrase&& p) { phrases.push_back(std::move(p)); });
        EXPECT_TRUE(nlohmann::json::sax_parse(in, &handler));

        ASSERT_EQ(phrases.size(), static_cast<std::size_t>(kCount));
        EXPECT_EQ(phrases.front().start_offset, 0u);
        EXPECT_EQ(phrases.back().start_offset, static_cast<std::size_t>(kCount - 1));
    }
}

// ── analyze() parallel orchestration ─────────────────────────────────────────

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

std::vector<std::string> phrase_length_filenames(const std::filesystem::path& dir) {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        auto name = entry.path().filename().string();
        if (name.rfind("phrases_length_", 0) == 0) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Compares analyze(dir, 1) against analyze(dir, n_threads) on the same input,
// asserting statistics.txt and all phrases_length_*.txt files are identical.
void expect_parallel_matches_serial(const std::string& json, std::size_t n_threads) {
    AnalyzeFixture serial_fix;
    AnalyzeFixture parallel_fix;
    serial_fix.write_results_json(json);
    parallel_fix.write_results_json(json);

    analyze(serial_fix.dir, 1);
    analyze(parallel_fix.dir, n_threads);

    EXPECT_EQ(read_file(serial_fix.dir / "statistics.txt"),
              read_file(parallel_fix.dir / "statistics.txt"));

    auto serial_files = phrase_length_filenames(serial_fix.dir);
    auto parallel_files = phrase_length_filenames(parallel_fix.dir);
    ASSERT_EQ(serial_files, parallel_files);
    for (const auto& name : serial_files) {
        EXPECT_EQ(read_file(serial_fix.dir / name), read_file(parallel_fix.dir / name)) << name;
    }
}

const char* kSixPhraseJson = R"({"phrases":[)"
    R"({"start_offset":10,"words":["aaa"]},)"
    R"({"start_offset":20,"words":["bbb"]},)"
    R"({"start_offset":30,"words":["ccc"]},)"
    R"({"start_offset":40,"words":["ddd"]},)"
    R"({"start_offset":50,"words":["eee"]},)"
    R"({"start_offset":60,"words":["fff"]})"
    R"(]})";

}  // namespace

TEST(AnalyzeParallel, MatchesSerial_EvenSplit) {
    expect_parallel_matches_serial(kSixPhraseJson, 2);
}

TEST(AnalyzeParallel, MatchesSerial_SinglePhrase) {
    expect_parallel_matches_serial(R"({"phrases":[{"start_offset":5,"words":["ab"]}]})", 4);
}

TEST(AnalyzeParallel, FallsBackForEmptyPhrases) {
    AnalyzeFixture fix;
    fix.write_results_json(R"({"phrases":[]})");

    analyze(fix.dir, 4);

    std::ifstream stats(fix.dir / "statistics.txt");
    EXPECT_TRUE(stats.good());
    EXPECT_TRUE(phrase_length_filenames(fix.dir).empty());
    EXPECT_FALSE(std::filesystem::exists(fix.dir / ".analyze_tmp"));
}

TEST(AnalyzeParallel, HandlesFewerPhrasesThanThreads) {
    expect_parallel_matches_serial(R"({"phrases":[)"
                                    R"({"start_offset":1,"words":["a"]},)"
                                    R"({"start_offset":2,"words":["b"]},)"
                                    R"({"start_offset":3,"words":["c"]})"
                                    R"(]})",
                                    8);
}

TEST(AnalyzeParallel, MalformedJsonThrows) {
    AnalyzeFixture fix;
    fix.write_results_json("not json at all");
    EXPECT_THROW(analyze(fix.dir, 4), nlohmann::json::exception);
}

TEST(AnalyzeParallel, CleansUpTempDir) {
    AnalyzeFixture fix;
    fix.write_results_json(kSixPhraseJson);

    analyze(fix.dir, 2);

    EXPECT_FALSE(std::filesystem::exists(fix.dir / ".analyze_tmp"));
}
