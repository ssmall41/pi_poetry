#pragma once
#include <algorithm>
#include <fstream>
#include <optional>
#include <string>

// Returns digits from the file (trailing newline stripped), or nullopt if the
// file cannot be opened or contains non-digit characters.
inline std::optional<std::string>
read_and_validate_input_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::string digits;
    std::getline(in, digits);
    if (!std::ranges::all_of(digits, [](unsigned char c) { return std::isdigit(c) != 0; }))
        return std::nullopt;
    return digits;
}
