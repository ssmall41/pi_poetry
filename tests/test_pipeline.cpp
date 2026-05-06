#include <gtest/gtest.h>
#include "pipeline/Pipeline.hpp"
#include "digit_source/FileDigitSource.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include <filesystem>
#include <fstream>
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

// --- All three output files appear ---

TEST(Pipeline_RunDir, WritesAllThreeOutputFilesInRunDir) {
    PipelineFixture fix;
    auto pipeline = fix.make();
    pipeline.run(fix.run_dir);
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "results.txt"));
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "results.json"));
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "debug_letters.txt"));
}

// --- Letters file content is correct ---

TEST(Pipeline_RunDir, LettersFileContainsExpectedContent) {
    PipelineFixture fix;
    auto pipeline = fix.make();
    pipeline.run(fix.run_dir);

    std::ifstream f(fix.run_dir / "debug_letters.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "fphab\n");
}
