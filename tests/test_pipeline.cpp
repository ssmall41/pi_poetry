#include <gtest/gtest.h>
#include "pipeline/Pipeline.hpp"
#include "digit_source/FileDigitSource.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include "result_analyzer/ResultAnalyzer.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace {

std::filesystem::path make_temp_digit_file(const std::string& digits) {
    auto p = std::filesystem::temp_directory_path() / "pi_pipeline_test_digits.txt";
    std::ofstream f(p);
    f << digits;
    return p;
}

struct PipelineFixture {
    std::filesystem::path digit_file = make_temp_digit_file("3141592653");
    std::filesystem::path run_dir    = std::filesystem::temp_directory_path() / "pi_pipeline_run_test";

    FileDigitSource    source{digit_file.string()};
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU     finder;
    HumanReviewScanner scanner{5};

    PipelineFixture() {
        finder.build();
        std::filesystem::create_directories(run_dir);
    }

    ~PipelineFixture() { std::filesystem::remove_all(run_dir); }

    Pipeline make() { return Pipeline(source, mapper, finder, scanner); }
};

}  // namespace

// --- Tracer bullet: results.txt appears in run_dir ---

TEST(Pipeline_RunDir, WritesTextFileInRunDir) {
    PipelineFixture fix;
    auto pipeline = fix.make();
    pipeline.run(fix.run_dir);
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "results.txt"));
}

// --- Letter file: written when enabled, skipped when disabled ---

TEST(Pipeline_RunDir, WritesLetterFileWhenEnabled) {
    PipelineFixture fix;
    auto pipeline = fix.make();
    pipeline.run(fix.run_dir, true);
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "results.txt"));
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "results.json"));
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "letter_sequence.txt"));
}

TEST(Pipeline_RunDir, SkipsLetterFileWhenDisabled) {
    PipelineFixture fix;
    auto pipeline = fix.make();
    pipeline.run(fix.run_dir);
    EXPECT_FALSE(std::filesystem::exists(fix.run_dir / "letter_sequence.txt"));
}

// --- Letters file content is correct ---

TEST(Pipeline_RunDir, LettersFileContainsExpectedContent) {
    PipelineFixture fix;
    auto pipeline = fix.make();
    pipeline.run(fix.run_dir, true);

    std::ifstream f(fix.run_dir / "letter_sequence.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "fphab\n");
}

// --- AllCombos: all sequences are written to results ---

// "0001" → TwoDigitBlockMapper → "ab" (a=0%26='a', b=1%26='b')
// AllCombos with {a, ab, b} yields sequences: [a], [a,b], [ab], [b]
// process_words (max_gap=0): "a"@0, "a b [gaps: 0]"@0, "ab"@0, "b"@1
// After dedup+sort: 4 distinct phrases; ETL would only produce 1

TEST(Pipeline_AllCombos, ProducesAllSequencesInResults) {
    auto digit_file = make_temp_digit_file("0001");
    auto run_dir    = std::filesystem::temp_directory_path() / "pi_allcombos_test";
    std::filesystem::create_directories(run_dir);

    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.insert_word("a");
    finder.insert_word("ab");
    finder.insert_word("b");
    finder.build();
    finder.set_overlap_policy(OverlapPolicy::AllCombos);
    HumanReviewScanner scanner(0);

    Pipeline pipeline(source, mapper, finder, scanner);
    pipeline.run(run_dir);

    std::ifstream f(run_dir / "results.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    std::filesystem::remove_all(run_dir);

    EXPECT_NE(contents.find("Offset 0: ab\n"),      std::string::npos);
    EXPECT_NE(contents.find("Offset 0: a b"),        std::string::npos);
    EXPECT_NE(contents.find("Offset 1: b\n"),        std::string::npos);

    int lines = 0;
    for (char c : contents) if (c == '\n') ++lines;
    EXPECT_GT(lines, 1);
}

TEST(Pipeline_AllCombos, SortsByOffsetThenFirstWord) {
    // Within the same offset, phrases are ordered by their first word alphabetically.
    // "a ..." (first word "a") < "ab" (first word "ab") — expect "a" lines to precede "ab" line.
    auto digit_file = make_temp_digit_file("0001");
    auto run_dir    = std::filesystem::temp_directory_path() / "pi_sort_test";
    std::filesystem::create_directories(run_dir);

    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.insert_word("a");
    finder.insert_word("ab");
    finder.insert_word("b");
    finder.build();
    finder.set_overlap_policy(OverlapPolicy::AllCombos);
    HumanReviewScanner scanner(0);

    Pipeline pipeline(source, mapper, finder, scanner);
    pipeline.run(run_dir);

    std::ifstream f(run_dir / "results.txt");
    ASSERT_TRUE(f.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) if (!line.empty()) lines.push_back(line);
    std::filesystem::remove_all(run_dir);

    ASSERT_GE(lines.size(), 3u);

    // All Offset-0 lines come before Offset-1 lines
    auto last_offset0 = std::string::npos;
    auto first_offset1 = std::string::npos;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("Offset 0:") != std::string::npos) last_offset0 = i;
        if (first_offset1 == std::string::npos &&
            lines[i].find("Offset 1:") != std::string::npos) first_offset1 = i;
    }
    if (last_offset0 != std::string::npos && first_offset1 != std::string::npos) {
        EXPECT_LT(last_offset0, first_offset1);
    }

    // Within offset 0: line "Offset 0: a ..." appears before "Offset 0: ab"
    std::size_t line_a  = std::string::npos;
    std::size_t line_ab = std::string::npos;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("Offset 0: a ") != std::string::npos && line_a == std::string::npos)
            line_a = i;
        if (lines[i] == "Offset 0: ab") line_ab = i;
    }
    if (line_a != std::string::npos && line_ab != std::string::npos) {
        EXPECT_LT(line_a, line_ab);
    }
}

// ── Overlap policy: ETL ⊆ AllCombos ─────────────────────────────────────────

TEST(Pipeline_OverlapPolicy, ETLPhrasesAreSubsetOfAllCombos) {
    namespace fs = std::filesystem;
    using Phrases = std::set<std::pair<std::size_t, std::vector<std::string>>>;

    auto parse_phrases = [](const fs::path& dir) {
        std::ifstream f(dir / "results.json");
        auto j = nlohmann::json::parse(f);
        Phrases result;
        for (const auto& p : j["phrases"])
            result.insert({p["start_offset"].get<std::size_t>(),
                           p["words"].get<std::vector<std::string>>()});
        return result;
    };

    const std::string dict = PI_POETRY_SOURCE_DIR "/dictionaries/english_trimmed.txt";
    const std::string pi   = PI_POETRY_SOURCE_DIR "/data/pi_2000.txt";

    auto etl_dir = fs::temp_directory_path() / "pi_etl_subset_test";
    auto ac_dir  = fs::temp_directory_path() / "pi_ac_subset_test";
    fs::create_directories(etl_dir);
    fs::create_directories(ac_dir);

    {   // ETL run (default policy, max_gap=0 for consecutive-only phrases)
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner{0};
        Pipeline{source, mapper, finder, scanner}.run(etl_dir);
    }
    {   // AllCombos run (max_gap=0 for consecutive-only phrases)
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner{0};
        Pipeline{source, mapper, finder, scanner}.run(ac_dir);
    }

    auto etl_phrases = parse_phrases(etl_dir);
    auto ac_phrases  = parse_phrases(ac_dir);
    fs::remove_all(etl_dir);
    fs::remove_all(ac_dir);

    for (const auto& [offset, words] : etl_phrases) {
        std::string word_list;
        for (const auto& w : words)
            word_list += (word_list.empty() ? "" : ", ") + w;
        EXPECT_TRUE(ac_phrases.count({offset, words}) > 0)
            << "ETL phrase missing from AllCombos — offset=" << offset
            << " words=[" << word_list << "]";
    }
}

// ── Pipeline + analysis integration ──────────────────────────────────────────

TEST(Pipeline_Analysis, AnalyzeAfterRunProducesStatisticsAndPhraseLengthFiles) {
    // Uses the real pi_2000.txt digit file so results.json has real content.
    std::filesystem::path digit_file{PI_POETRY_SOURCE_DIR "/data/pi_2000.txt"};
    auto run_dir = std::filesystem::temp_directory_path() / "pi_pipeline_analysis_test";
    std::filesystem::create_directories(run_dir);

    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.load_dictionary(PI_POETRY_SOURCE_DIR "/dictionaries/english_trimmed.txt");
    finder.build();
    HumanReviewScanner scanner(5);

    Pipeline pipeline(source, mapper, finder, scanner);
    pipeline.run(run_dir);
    result_analyzer::analyze(run_dir);

    EXPECT_TRUE(std::filesystem::exists(run_dir / "statistics.txt"));

    bool found_phrase_file = false;
    for (const auto& entry : std::filesystem::directory_iterator(run_dir)) {
        if (entry.path().filename().string().rfind("phrases_length_", 0) == 0)
            found_phrase_file = true;
    }
    EXPECT_TRUE(found_phrase_file);

    std::filesystem::remove_all(run_dir);
}
