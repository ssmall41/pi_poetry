#pragma once
#include "DigitSource.hpp"
#include <fstream>
#include <mutex>
#include <string>

class FileDigitSource final : public DigitSource {
public:
    explicit FileDigitSource(const std::string& path);

    std::size_t next_chunk(uint8_t* buffer, std::size_t n) override;
    void reset() override;
    bool is_finite() const override { return true; }
    std::optional<uint64_t> estimated_length() const override;
    int base() const override { return 10; }

private:
    std::string path_;
    std::ifstream file_;
    mutable std::mutex mutex_;
    uint64_t digit_count_{0};
};
