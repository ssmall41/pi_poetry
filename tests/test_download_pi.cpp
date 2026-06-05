#include <gtest/gtest.h>
#include "download_pi_utils.h"
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path make_temp_file(const std::string& content) {
    auto p = std::filesystem::temp_directory_path() / "pi_download_test.txt";
    std::ofstream f(p, std::ios::binary);
    f << content;
    return p;
}

}  // namespace

TEST(GetExistingDigitCount, ValidFile_ReturnsSize) {
    auto p = make_temp_file("31415");
    auto result = get_existing_digit_count(p.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 5);
}

TEST(GetExistingDigitCount, FileEndingInNewline_ReturnsNullopt) {
    auto p = make_temp_file("31415\n");
    auto result = get_existing_digit_count(p.string());
    EXPECT_FALSE(result.has_value());
}

TEST(GetExistingDigitCount, FileEndingInSpace_ReturnsNullopt) {
    auto p = make_temp_file("31415 ");
    auto result = get_existing_digit_count(p.string());
    EXPECT_FALSE(result.has_value());
}

TEST(GetExistingDigitCount, NonExistentFile_ReturnsNullopt) {
    auto result = get_existing_digit_count("/tmp/does_not_exist_pi_test.txt");
    EXPECT_FALSE(result.has_value());
}

TEST(GetExistingDigitCount, EmptyFile_ReturnsZero) {
    auto p = make_temp_file("");
    auto result = get_existing_digit_count(p.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
}
