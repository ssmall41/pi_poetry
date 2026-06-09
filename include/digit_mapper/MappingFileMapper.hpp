#pragma once
#include "digit_mapper/DigitMapper.hpp"
#include <filesystem>
#include <string>
#include <vector>

class MappingFileMapper : public DigitMapper {
    int digits_per_char_;
    int base_;
    std::vector<char> table_;
    std::string alphabet_;

public:
    explicit MappingFileMapper(const std::filesystem::path& path);

    void map(const uint8_t* digits, std::size_t n_digits,
             char* out_chars, std::size_t& out_n) override;
    int digits_per_char() const override { return digits_per_char_; }
    std::size_t alphabet_size() const override { return alphabet_.size(); }
    std::string_view alphabet() const override { return alphabet_; }
    int required_base() const override { return base_; }
};
