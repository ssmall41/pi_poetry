#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace gen_mapping {

std::map<char, std::size_t> count_char_frequencies(const std::string& text);

struct AllocationResult {
    std::map<char, std::size_t> slots;
    std::size_t total_slots;
};

// throws std::invalid_argument if unique char count > total_slots
AllocationResult allocate_slots(const std::map<char, std::size_t>& frequencies, int digits);

// Returns (combo_string, char) pairs ordered by character block, pre-shuffle
std::vector<std::pair<std::string, char>> assign_combos(
    const AllocationResult& alloc, int digits);

// seed==nullopt uses random_device; otherwise seeds mt19937_64 with given value
void shuffle_assignments(
    std::vector<std::pair<std::string, char>>& assignments,
    std::optional<uint64_t> seed);

void write_mapping(
    const std::vector<std::pair<std::string, char>>& assignments,
    int digits, std::ostream& out);

std::string read_dict_file(const std::filesystem::path& path);

void write_mapping_file(
    const std::vector<std::pair<std::string, char>>& assignments,
    int digits, const std::filesystem::path& path);

}  // namespace gen_mapping
