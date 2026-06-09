#pragma once
#include "DigitMapper.hpp"

// Maps consecutive pairs of base-10 digits to letters a–z.
// Both the alphabet (a–z, 26 letters) and the base (10) are hardcoded
// assumptions — they are not configurable.
class TwoDigitBlockMapper final : public DigitMapper {
public:
    void map(const uint8_t* digits, std::size_t n_digits,
             char* out_chars, std::size_t& out_n) override;

    int digits_per_char() const override { return 2; }
    std::size_t alphabet_size() const override { return 26; }          // assumed: a–z
    std::string_view alphabet() const override { return "abcdefghijklmnopqrstuvwxyz"; }  // assumed: a–z
    int required_base() const override { return 10; }                  // assumed: base 10
};
