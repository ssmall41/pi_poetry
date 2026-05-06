#pragma once
#include "DigitMapper.hpp"

class TwoDigitBlockMapper final : public DigitMapper {
public:
    void map(const uint8_t* digits, std::size_t n_digits,
             char* out_chars, std::size_t& out_n) override;

    int digits_per_char() const override { return 2; }
    std::size_t alphabet_size() const override { return 26; }
    std::string_view alphabet() const override { return "abcdefghijklmnopqrstuvwxyz"; }
    int required_base() const override { return 10; }
};
