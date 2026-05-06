#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

class DigitMapper {
public:
    virtual ~DigitMapper() = default;

    // Converts n_digits digits into characters written to out_chars.
    // n_digits must be a multiple of digits_per_char().
    // out_n is set to the number of characters written.
    virtual void map(const uint8_t* digits, std::size_t n_digits,
                     char* out_chars, std::size_t& out_n) = 0;

    virtual int digits_per_char() const = 0;
    virtual std::size_t alphabet_size() const = 0;
    virtual std::string_view alphabet() const = 0;

    // The numeric base this mapper expects (must match DigitSource::base()).
    virtual int required_base() const = 0;
};
