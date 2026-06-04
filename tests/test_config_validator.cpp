#include "config_validator.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
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
    cfg["pipeline"].as_table()->insert_or_assign("mode", "batch");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "pipeline.mode"));
    EXPECT_TRUE(any_error_contains(errors, "serial"));
}

TEST(ConfigValidator, PipelineModeParallelIsValid) {
    auto cfg = make_valid_table();
    cfg["pipeline"].as_table()->insert_or_assign("mode", "parallel");
    EXPECT_TRUE(validate_config(cfg).empty());
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

// ── Reserved integer fields ───────────────────────────────────────────────────

TEST(ConfigValidator, InvalidDigitSourceThreads) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("threads", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.threads"));
}

TEST(ConfigValidator, DigitSourceThreadsMultipleIsValid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("threads", 4);
    EXPECT_TRUE(validate_config(cfg).empty());
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
    cfg["digit_mapper"].as_table()->insert_or_assign("threads", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_mapper.threads"));
}

TEST(ConfigValidator, InvalidWordFinderThreads) {
    auto cfg = make_valid_table();
    cfg["word_finder"].as_table()->insert_or_assign("threads", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "word_finder.threads"));
}

TEST(ConfigValidator, InvalidPhraseScannerThreads) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("threads", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.threads"));
}

// ── Active fields ─────────────────────────────────────────────────────────────

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
    cfg["pipeline"].as_table()->insert_or_assign("mode", "batch");       // invalid mode
    cfg["digit_source"].as_table()->insert_or_assign("threads", 0);      // invalid: < 1
    cfg["word_finder"].as_table()->insert_or_assign("min_word_length", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 3u);
    EXPECT_TRUE(any_error_contains(errors, "pipeline.mode"));
    EXPECT_TRUE(any_error_contains(errors, "digit_source.threads"));
    EXPECT_TRUE(any_error_contains(errors, "word_finder.min_word_length"));
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

// ── digit_source.chunk_size ──────────────────────────────────────────────────

TEST(ConfigValidator, ChunkSizeOneIsValid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("chunk_size", 1);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, ChunkSizeZeroIsInvalid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("chunk_size", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.chunk_size"));
    EXPECT_TRUE(any_error_contains(errors, ">= 1"));
}

TEST(ConfigValidator, ChunkSizeWrongTypeIsInvalid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("chunk_size", "big");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.chunk_size"));
}

// ── [analysis] section ────────────────────────────────────────────────────────

TEST(ConfigValidator, AnalysisRunAfterPipelineBoolIsValid) {
    auto cfg = make_valid_table();
    cfg.insert_or_assign("analysis", toml::table{});
    cfg["analysis"].as_table()->insert_or_assign("run_after_pipeline", false);
    EXPECT_TRUE(validate_config(cfg).empty());
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

// ── phrase_scanner.min_phrase_length ──────────────────────────────────────────

TEST(ConfigValidator, MinPhraseLength1IsValid) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("min_phrase_length", 1);
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MinPhraseLengthZeroIsInvalid) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("min_phrase_length", 0);
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.min_phrase_length"));
    EXPECT_TRUE(any_error_contains(errors, ">= 1"));
}

TEST(ConfigValidator, MinPhraseLengthWrongTypeIsInvalid) {
    auto cfg = make_valid_table();
    cfg["phrase_scanner"].as_table()->insert_or_assign("min_phrase_length", "two");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "phrase_scanner.min_phrase_length"));
}

// ── digit_source type = "api" ─────────────────────────────────────────────────

static std::filesystem::path make_temp_api_source_config() {
    auto p = std::filesystem::temp_directory_path() / "pi_test_api_source.toml";
    std::ofstream f(p);
    f << "[api]\n"
         "base_url        = \"http://localhost\"\n"
         "start_param     = \"start\"\n"
         "count_param     = \"n\"\n"
         "max_per_request = 1000\n\n"
         "[response]\n"
         "digits_json_field = \"content\"\n";
    return p;
}

static toml::table make_valid_api_table() {
    auto sc_path = make_temp_api_source_config();
    return toml::parse(
        "[pipeline]\n"
        "mode = \"serial\"\n"
        "\n"
        "[digit_source]\n"
        "type          = \"api\"\n"
        "source_config = \"" + sc_path.string() + "\"\n"
        "threads       = 1\n"
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

TEST(ConfigValidator, ApiTypeIsValid) {
    auto cfg = make_valid_api_table();
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, ApiTypeMissingSourceConfigIsInvalid) {
    auto cfg = make_valid_api_table();
    cfg["digit_source"].as_table()->erase("source_config");
    auto errors = validate_config(cfg);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.source_config"));
}

TEST(ConfigValidator, ApiTypeSourceConfigMissingFileIsInvalid) {
    auto cfg = make_valid_api_table();
    cfg["digit_source"].as_table()->insert_or_assign("source_config",
                                                     "/nonexistent/source.toml");
    auto errors = validate_config(cfg);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.source_config"));
}

TEST(ConfigValidator, FileTypePathNotCheckedForApiType) {
    auto cfg = make_valid_api_table();
    // No "path" field present — must not produce an error for type = "api"
    cfg["digit_source"].as_table()->erase("path");
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, InvalidTypeNeitherFileNorApi) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("type", "stream");
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.type"));
    EXPECT_TRUE(any_error_contains(errors, "file"));
    EXPECT_TRUE(any_error_contains(errors, "api"));
}

// ── digit_source.max_digits ───────────────────────────────────────────────────

TEST(ConfigValidator, MaxDigitsZeroIsValid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("max_digits", int64_t{0});
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MaxDigitsPositiveIsValid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("max_digits", int64_t{100000});
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MaxDigitsNegativeIsInvalid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("max_digits", int64_t{-1});
    auto errors = validate_config(cfg);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "digit_source.max_digits"));
}

TEST(ConfigValidator, MaxDigitsSetForFileTypeIsValid) {
    auto cfg = make_valid_table();
    cfg["digit_source"].as_table()->insert_or_assign("max_digits", int64_t{5000});
    EXPECT_TRUE(validate_config(cfg).empty());
}

TEST(ConfigValidator, MaxDigitsSetForApiTypeIsValid) {
    auto cfg = make_valid_api_table();
    cfg["digit_source"].as_table()->insert_or_assign("max_digits", int64_t{5000});
    EXPECT_TRUE(validate_config(cfg).empty());
}
