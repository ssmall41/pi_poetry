#include "digit_mapper/TwoDigitBlockMapper.hpp"

void TwoDigitBlockMapper::map(const uint8_t* digits, std::size_t n_digits,
                              char* out_chars, std::size_t& out_n) {
    out_n = 0;
    const std::size_t pairs = n_digits / 2;
    for (std::size_t i = 0; i < pairs; ++i) {
        int val = digits[2 * i] * 10 + digits[2 * i + 1];  // base 10 assumed
        out_chars[out_n++] = static_cast<char>('a' + val % 26);  // 26-letter alphabet assumed
    }
}
