#include <gtest/gtest.h>
#include "digit_mapper/MappingFileMapper.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <vector>

namespace {

const std::filesystem::path TWO_DIGIT_BLOCK_FILE =
    std::filesystem::path(PI_POETRY_SOURCE_DIR) / "config/mappings/two_digit_block.txt";

std::string map_digits(MappingFileMapper& m, std::initializer_list<uint8_t> digits) {
    std::vector<uint8_t> d(digits);
    std::vector<char> out(d.size() / static_cast<std::size_t>(m.digits_per_char()));
    std::size_t n = 0;
    m.map(d.data(), d.size(), out.data(), n);
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n)};
}

std::filesystem::path write_temp(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() /
                ("pi_poetry_mfm_test_" + std::to_string(
                    std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".txt");
    std::ofstream f(path);
    f << content;
    return path;
}

}  // namespace

TEST(MappingFileMapper, ParseProperties) {
    MappingFileMapper m(TWO_DIGIT_BLOCK_FILE);
    EXPECT_EQ(m.digits_per_char(), 2);
    EXPECT_EQ(m.required_base(), 10);
    EXPECT_EQ(m.alphabet_size(), 26u);
    EXPECT_EQ(m.alphabet(), "abcdefghijklmnopqrstuvwxyz");
}

TEST(MappingFileMapper, MatchesTwoDigitBlockMapper) {
    MappingFileMapper file_m(TWO_DIGIT_BLOCK_FILE);
    TwoDigitBlockMapper code_m;

    std::vector<std::initializer_list<uint8_t>> inputs = {
        {0,0}, {0,1}, {2,5}, {2,6}, {5,1}, {5,2}, {7,7}, {7,8}, {9,9},
        {0,0, 2,5, 2,6, 5,1}
    };
    for (auto& input : inputs) {
        std::vector<uint8_t> d(input);
        std::vector<char> file_out(d.size() / 2), code_out(d.size() / 2);
        std::size_t fn = 0, cn = 0;
        file_m.map(d.data(), d.size(), file_out.data(), fn);
        code_m.map(d.data(), d.size(), code_out.data(), cn);
        EXPECT_EQ(fn, cn);
        EXPECT_EQ(std::string(file_out.begin(), file_out.begin() + static_cast<std::ptrdiff_t>(fn)),
                  std::string(code_out.begin(), code_out.begin() + static_cast<std::ptrdiff_t>(cn)));
    }
}

TEST(MappingFileMapper, PiPrefix) {
    MappingFileMapper m(TWO_DIGIT_BLOCK_FILE);
    // 31->f(5), 41->p(15), 59->h(7), 26->a(0)
    EXPECT_EQ(map_digits(m, {3,1,4,1,5,9,2,6}), "fpha");
}

TEST(MappingFileMapper, FileNotFound) {
    EXPECT_THROW(MappingFileMapper{"/nonexistent/path/mapping.txt"}, std::runtime_error);
}

TEST(MappingFileMapper, MissingEntry) {
    // base=2, digits_per_char=1 → need entries "0 x" and "1 y"; omit "1 y"
    auto path = write_temp("digits_per_char=1\nbase=2\n0 a\n");
    EXPECT_THROW(MappingFileMapper{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(MappingFileMapper, DuplicateEntry) {
    auto path = write_temp("digits_per_char=1\nbase=2\n0 a\n0 b\n1 c\n");
    EXPECT_THROW(MappingFileMapper{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(MappingFileMapper, MalformedLine) {
    // Entry with no space separator
    auto path = write_temp("digits_per_char=1\nbase=2\n0a\n1 b\n");
    EXPECT_THROW(MappingFileMapper{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(MappingFileMapper, MissingDigitsPerCharHeader) {
    auto path = write_temp("base=2\n0 a\n1 b\n");
    EXPECT_THROW(MappingFileMapper{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(MappingFileMapper, MissingBaseHeader) {
    auto path = write_temp("digits_per_char=1\n0 a\n1 b\n");
    EXPECT_THROW(MappingFileMapper{path}, std::runtime_error);
    std::filesystem::remove(path);
}
