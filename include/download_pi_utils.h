#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

// Returns the number of digits already in the file (file_size == digit_count
// since no newlines are written), or nullopt if the file cannot be opened or
// ends in whitespace. Returns 0 for an empty file.
inline std::optional<int64_t>
get_existing_digit_count(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return std::nullopt;
    if (size == 0) return int64_t{0};

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(-1, std::ios::end);
    char c;
    if (!in.get(c)) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(c))) return std::nullopt;

    return static_cast<int64_t>(size);
}
