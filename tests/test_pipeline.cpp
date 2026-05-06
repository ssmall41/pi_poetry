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
    std::filesystem::path digit_file  = make_temp_digit_file("3141592653");
    std::filesystem::path text_out    = std::filesystem::temp_directory_path() / "pi_pipeline_test_out.txt";
    std::filesystem::path json_out    = std::filesystem::temp_directory_path() / "pi_pipeline_test_out.json";
    std::filesystem::path letters_out = std::filesystem::temp_directory_path() / "pi_pipeline_test_letters.txt";

    FileDigitSource   source{digit_file.string()};
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU    finder;
    HumanReviewScanner scanner{5};

    PipelineFixture() { finder.build(); }

    Pipeline make() { return Pipeline(source, mapper, finder, scanner); }
};

}  // namespace

TEST(Pipeline_Letters, WritesFileWhenPathSet) {
    PipelineFixture fix;
    auto pipeline = fix.make();
    pipeline.run(fix.text_out.string(), fix.json_out.string(), fix.letters_out.string());

    std::ifstream f(fix.letters_out);
    ASSERT_TRUE(f.is_open()) << "letters file was not created";
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "fphab\n");
}

TEST(Pipeline_Letters, NoFileWhenPathEmpty) {
    PipelineFixture fix;
    std::filesystem::remove(fix.letters_out);  // ensure absent before run
    auto pipeline = fix.make();
    pipeline.run(fix.text_out.string(), fix.json_out.string());

    EXPECT_FALSE(std::filesystem::exists(fix.letters_out));
}
