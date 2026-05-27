#include <gtest/gtest.h>
#include "download_pi_utils.h"
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path make_temp_file(const std::string& content) {
    auto p = std::filesystem::temp_directory_path() / "pi_download_test.txt";
    std::ofstream f(p);
    f << content;
    return p;
}

}  // namespace

TEST(ReadAndValidateInputFile, ValidFile_ReturnsStrippedDigits) {
    auto p = make_temp_file("31415\n");
    auto result = read_and_validate_input_file(p.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "31415");
}

TEST(ReadAndValidateInputFile, NonDigitChars_ReturnsNullopt) {
    auto p = make_temp_file("3141X\n");
    auto result = read_and_validate_input_file(p.string());
    EXPECT_FALSE(result.has_value());
}

TEST(ReadAndValidateInputFile, NonExistentFile_ReturnsNullopt) {
    auto result = read_and_validate_input_file("/tmp/does_not_exist_pi_test.txt");
    EXPECT_FALSE(result.has_value());
}

TEST(ReadAndValidateInputFile, EmptyFile_ReturnsEmptyString) {
    auto p = make_temp_file("\n");
    auto result = read_and_validate_input_file(p.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "");
}
