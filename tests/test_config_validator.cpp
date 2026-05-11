#include "config_validator.hpp"
#include <gtest/gtest.h>
#include <string>

// Builds a fully valid toml::table using real paths from the source tree.
static toml::table make_valid_table() {
    return toml::parse(
        "[pipeline]\n"
        "mode = \"serial\"\n"
        "\n"
        "[digit_source]\n"
        "type    = \"file\"\n"
        "path    = \"" PI_POETRY_SOURCE_DIR "/data/pi_2000.txt\"\n"
        "threads = 1\n"
        "\n"
        "[digit_mapper]\n"
        "type     = \"two-digit-block\"\n"
        "alphabet = \"alpha-lower\"\n"
        "base     = 10\n"
        "threads  = 1\n"
        "\n"
        "[word_finder]\n"
        "type            = \"aho-corasick-cpu\"\n"
        "dictionary      = \"" PI_POETRY_SOURCE_DIR "/dictionaries/english.txt\"\n"
        "overlap_policy  = \"earliest-then-longest\"\n"
        "min_word_length = 1\n"
        "threads         = 1\n"
        "\n"
        "[phrase_scanner]\n"
        "type    = \"human-review\"\n"
        "mode    = \"gap-tolerant\"\n"
        "max_gap = 0\n"
        "threads = 1\n"
        "\n"
        "[output]\n"
        "dir = \"outputs\"\n"
    );
}

// Helper: return true if any error string contains needle
static bool any_error_contains(const std::vector<std::string>& errors,
                                const std::string& needle) {
    for (const auto& e : errors)
        if (e.find(needle) != std::string::npos) return true;
    return false;
}

// ── Happy path ────────────────────────────────────────────────────────────────

TEST(ConfigValidator, ValidConfigReturnsNoErrors) {
    auto cfg = make_valid_table();
    auto errors = validate_config(cfg);
    EXPECT_TRUE(errors.empty());
}

// ── Reserved string fields ────────────────────────────────────────────────────

TEST(ConfigValidator, InvalidPipelineMode) {
    auto cfg = make_valid_table();
    cfg["pipeline"].as_table()->insert_or_assign("mode", "parallel");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "pipeline.mode"));
    EXPECT_TRUE(any_error_contains(errors, "serial"));
}

TEST(ConfigValidator, InvalidDigitSourceType) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("type", "stream");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.type"));
    EXPECT_TRUE(any_error_contains(errors, "file"));
}

TEST(ConfigValidator, InvalidDigitMapperType) {
    auto cfg = make_valid_table();
    cfg["digit_mapper"].as_table()->insert_or_assign("type", "one-digit");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_mapper.type"));
    EXPECT_TRUE(any_error_contains(errors, "two-digit-block"));
}

TEST(ConfigValidator, InvalidDigitMapperAlphabet) {
    auto cfg = make_valid_table();
    cfg["digit_mapper"].as_table()->insert_or_assign("alphabet", "alpha-upper");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_mapper.alphabet"));
    EXPECT_TRUE(any_error_contains(errors, "alpha-lower"));
}

TEST(ConfigValidator, InvalidWordFinderType) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("type", "naive");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "word_finder.type"));
    EXPECT_TRUE(any_error_contains(errors, "aho-corasick-cpu"));
}

TEST(ConfigValidator, InvalidPhraseScannerType) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("type", "auto");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.type"));
    EXPECT_TRUE(any_error_contains(errors, "human-review"));
}

TEST(ConfigValidator, InvalidPhraseScannerMode) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("mode", "strict");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.mode"));
    EXPECT_TRUE(any_error_contains(errors, "gap-tolerant"));
}

// ── Reserved integer fields ───────────────────────────────────────────────────

TEST(ConfigValidator, InvalidDigitSourceThreads) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("threads", 4);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.threads"));
    EXPECT_TRUE(any_error_contains(errors, "1"));
}

TEST(ConfigValidator, InvalidDigitMapperBase) {
    auto cfg = make_valid_table();
    cfg["digit_mapper"].as_table()->insert_or_assign("base", 16);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_mapper.base"));
    EXPECT_TRUE(any_error_contains(errors, "10"));
}

TEST(ConfigValidator, InvalidDigitMapperThreads) {
    auto cfg = make_valid_table();
    cfg["digit_mapper"].as_table()->insert_or_assign("threads", 2);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_mapper.threads"));
    EXPECT_TRUE(any_error_contains(errors, "1"));
}

TEST(ConfigValidator, InvalidWordFinderThreads) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("threads", 8);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "word_finder.threads"));
    EXPECT_TRUE(any_error_contains(errors, "1"));
}

TEST(ConfigValidator, InvalidPhraseScannerThreads) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("threads", 3);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.threads"));
    EXPECT_TRUE(any_error_contains(errors, "1"));
}

// ── Active fields ─────────────────────────────────────────────────────────────

TEST(ConfigValidator, OverlapPolicyEarliestThenLongestIsValid) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("overlap_policy",
                                                    "earliest-then-longest");
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, OverlapPolicyAllCombosIsValid) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("overlap_policy", "all-combos");
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, OverlapPolicyInvalidValue) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("overlap_policy", "random");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "word_finder.overlap_policy"));
    EXPECT_TRUE(any_error_contains(errors, "earliest-then-longest"));
    EXPECT_TRUE(any_error_contains(errors, "all-combos"));
}

TEST(ConfigValidator, MinWordLength1IsValid) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("min_word_length", 1);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MinWordLengthZeroIsInvalid) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("min_word_length", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "word_finder.min_word_length"));
    EXPECT_TRUE(any_error_contains(errors, ">= 1"));
}

TEST(ConfigValidator, MinWordLengthLargePositiveIsValid) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("min_word_length", 100);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MaxGapZeroIsValid) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("max_gap", 0);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MaxGapPositiveIsValid) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("max_gap", 100);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MaxGapNegativeIsInvalid) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("max_gap", -1);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.max_gap"));
    EXPECT_TRUE(any_error_contains(errors, ">= 0"));
}

// ── File existence ────────────────────────────────────────────────────────────

TEST(ConfigValidator, DigitSourcePathMissingFileIsInvalid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("path",
                                                     "/nonexistent/path/pi.txt");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.path"));
    EXPECT_TRUE(any_error_contains(errors, "/nonexistent/path/pi.txt"));
}

TEST(ConfigValidator, DictionaryMissingFileIsInvalid) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("dictionary",
                                                    "/no/such/dictionary.txt");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "word_finder.dictionary"));
    EXPECT_TRUE(any_error_contains(errors, "/no/such/dictionary.txt"));
}

TEST(ConfigValidator, OutputDirAnyStringIsValid) {
    auto cfg = make_valid_table();
    cfg["output"].as_table()->insert_or_assign("dir", "/weird/nonexistent/path");
    EXPECT_TRUE(validate_config(cfg).empty());
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST(ConfigValidator, MultipleErrorsAllReported) {
    auto cfg = make_valid_table();
    cfg["pipeline"].as_table()->insert_or_assign("mode", "parallel");
    cfg["digit_source"].as_table()->insert_or_assign("threads", 4);
    cfg["word_finder"].as_table()->insert_or_assign("min_word_length", 0);
    cfg["phrase_scanner"].as_table()->insert_or_assign("max_gap", -1);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 4u);
    EXPECT_TRUE(any_error_contains(errors, "pipeline.mode"));
    EXPECT_TRUE(any_error_contains(errors, "digit_source.threads"));
    EXPECT_TRUE(any_error_contains(errors, "word_finder.min_word_length"));
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.max_gap"));
}

TEST(ConfigValidator, MissingOverlapPolicyUsesDefault) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->erase("overlap_policy");
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, WrongTypeForIntegerField) {
    auto cfg = make_valid_table();
    cfg["digit_mapper"].as_table()->insert_or_assign("base", "ten");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_mapper.base"));
}

// ── [analysis] section ────────────────────────────────────────────────────────

TEST(ConfigValidator, AnalysisSectionAbsentIsValid) {
    auto cfg = make_valid_table();
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, AnalysisRunAfterPipelineFalseIsValid) {
    auto cfg = make_valid_table();
    cfg.insert_or_assign("analysis", toml::table{});
    cfg["analysis"].as_table()->insert_or_assign("run_after_pipeline", false);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, AnalysisRunAfterPipelineTrueIsValid) {
    auto cfg = make_valid_table();
    cfg.insert_or_assign("analysis", toml::table{});
    cfg["analysis"].as_table()->insert_or_assign("run_after_pipeline", true);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, AnalysisRunAfterPipelineWrongTypeIsInvalid) {
    auto cfg = make_valid_table();
    cfg.insert_or_assign("analysis", toml::table{});
    cfg["analysis"].as_table()->insert_or_assign("run_after_pipeline", "yes");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "analysis.run_after_pipeline"));
}
