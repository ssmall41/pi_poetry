#include <gtest/gtest.h>
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include <initializer_list>
#include <vector>

namespace {

std::string map_digits(TwoDigitBlockMapper& m,
                       std::initializer_list<uint8_t> digits) {
    std::vector<uint8_t> d(digits);
    std::vector<char> out(d.size() / 2);
    std::size_t n = 0;
    m.map(d.data(), d.size(), out.data(), n);
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n)};
}

}  // namespace

TEST(TwoDigitBlockMapper, Properties) {
    TwoDigitBlockMapper m;
    EXPECT_EQ(m.digits_per_char(), 2);
    EXPECT_EQ(m.alphabet_size(), 26u);
    EXPECT_EQ(m.required_base(), 10);
    EXPECT_EQ(m.alphabet(), "abcdefghijklmnopqrstuvwxyz");
}

TEST(TwoDigitBlockMapper, ModuloCycles) {
    TwoDigitBlockMapper m;
    EXPECT_EQ(map_digits(m, {0,0}), "a");  // 00 % 26 = 0
    EXPECT_EQ(map_digits(m, {0,1}), "b");  // 01 % 26 = 1
    EXPECT_EQ(map_digits(m, {2,5}), "z");  // 25 % 26 = 25
    EXPECT_EQ(map_digits(m, {2,6}), "a");  // 26 % 26 = 0
    EXPECT_EQ(map_digits(m, {5,1}), "z");  // 51 % 26 = 25
    EXPECT_EQ(map_digits(m, {5,2}), "a");  // 52 % 26 = 0
    EXPECT_EQ(map_digits(m, {7,7}), "z");  // 77 % 26 = 25
    EXPECT_EQ(map_digits(m, {7,8}), "a");  // 78 % 26 = 0
    EXPECT_EQ(map_digits(m, {9,9}), "v");  // 99 % 26 = 21
}

TEST(TwoDigitBlockMapper, PiPrefix) {
    TwoDigitBlockMapper m;
    // 31->f(5), 41->p(15), 59->h(7), 26->a(0)
    EXPECT_EQ(map_digits(m, {3,1,4,1,5,9,2,6}), "fpha");
}

TEST(TwoDigitBlockMapper, MultipleChars) {
    TwoDigitBlockMapper m;
    // All four pairs in one call
    std::vector<uint8_t> d = {0,0, 2,5, 2,6, 5,1};
    std::vector<char> out(4);
    std::size_t n = 0;
    m.map(d.data(), d.size(), out.data(), n);
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(std::string(out.begin(), out.end()), "azaz");
}
