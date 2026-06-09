#include "digit_mapper/MappingFileMapper.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

static int pow_int(int base, int exp) {
    int result = 1;
    for (int i = 0; i < exp; ++i) result *= base;
    return result;
}

static std::string trim(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

MappingFileMapper::MappingFileMapper(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("MappingFileMapper: cannot open file: " + path.string());

    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::string> entry_lines;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.find('=') != std::string::npos) {
            auto eq = line.find('=');
            headers.emplace_back(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
        } else {
            entry_lines.push_back(line);
        }
    }

    int dpc = -1, base = -1;
    for (const auto& [key, val] : headers) {
        if (key == "digits_per_char") {
            dpc = std::stoi(val);
            if (dpc < 1)
                throw std::runtime_error("MappingFileMapper: digits_per_char must be >= 1");
        } else if (key == "base") {
            base = std::stoi(val);
            if (base < 2 || base > 10)
                throw std::runtime_error("MappingFileMapper: base must be between 2 and 10");
        }
    }

    if (dpc < 0)
        throw std::runtime_error("MappingFileMapper: missing 'digits_per_char' header");
    if (base < 0)
        throw std::runtime_error("MappingFileMapper: missing 'base' header");

    digits_per_char_ = dpc;
    base_ = base;

    const int table_size = pow_int(base, dpc);
    table_.assign(static_cast<std::size_t>(table_size), '\0');
    std::vector<bool> seen(static_cast<std::size_t>(table_size), false);

    for (const auto& eline : entry_lines) {
        std::istringstream iss(eline);
        std::string combo, out_str;
        if (!(iss >> combo >> out_str))
            throw std::runtime_error("MappingFileMapper: malformed line: \"" + eline + "\"");

        if (static_cast<int>(combo.size()) != dpc)
            throw std::runtime_error(
                "MappingFileMapper: combo has wrong length in line: \"" + eline + "\"");

        if (out_str.size() != 1)
            throw std::runtime_error(
                "MappingFileMapper: output must be a single character in line: \"" + eline + "\"");

        int value = 0;
        for (char c : combo) {
            if (c < '0' || c >= '0' + base)
                throw std::runtime_error(
                    "MappingFileMapper: invalid digit '" + std::string(1, c) +
                    "' for base " + std::to_string(base) + " in line: \"" + eline + "\"");
            value = value * base + (c - '0');
        }

        if (seen[static_cast<std::size_t>(value)])
            throw std::runtime_error(
                "MappingFileMapper: duplicate entry for combo \"" + combo + "\"");

        seen[static_cast<std::size_t>(value)] = true;
        table_[static_cast<std::size_t>(value)] = out_str[0];
    }

    for (int i = 0; i < table_size; ++i) {
        if (!seen[static_cast<std::size_t>(i)]) {
            std::string combo(static_cast<std::size_t>(dpc), '0');
            int tmp = i;
            for (int j = dpc - 1; j >= 0; --j) {
                combo[static_cast<std::size_t>(j)] = '0' + (tmp % base);
                tmp /= base;
            }
            throw std::runtime_error(
                "MappingFileMapper: missing entry for combo \"" + combo + "\"");
        }
    }

    std::unordered_set<char> seen_chars;
    for (int i = 0; i < table_size; ++i) {
        char c = table_[static_cast<std::size_t>(i)];
        if (seen_chars.insert(c).second)
            alphabet_ += c;
    }
}

void MappingFileMapper::map(const uint8_t* digits, std::size_t n_digits,
                            char* out_chars, std::size_t& out_n) {
    out_n = 0;
    const std::size_t blocks = n_digits / static_cast<std::size_t>(digits_per_char_);
    for (std::size_t i = 0; i < blocks; ++i) {
        int value = 0;
        for (int j = 0; j < digits_per_char_; ++j)
            value = value * base_ + digits[i * static_cast<std::size_t>(digits_per_char_) + static_cast<std::size_t>(j)];
        out_chars[out_n++] = table_[static_cast<std::size_t>(value)];
    }
}
