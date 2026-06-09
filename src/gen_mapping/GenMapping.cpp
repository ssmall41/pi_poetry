#include "gen_mapping/GenMapping.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>

namespace gen_mapping {

std::map<char, std::size_t> count_char_frequencies(const std::string& text) {
    std::map<char, std::size_t> freq;
    for (unsigned char c : text) {
        if (std::isalpha(c))
            ++freq[static_cast<char>(std::tolower(c))];
    }
    return freq;
}

AllocationResult allocate_slots(const std::map<char, std::size_t>& frequencies, int digits) {
    std::size_t total_slots = 1;
    for (int i = 0; i < digits; ++i) total_slots *= 10;

    if (frequencies.size() > total_slots)
        throw std::invalid_argument(
            "dictionary has " + std::to_string(frequencies.size()) +
            " unique characters but digits=" + std::to_string(digits) +
            " only provides " + std::to_string(total_slots) + " slots");

    // Guarantee each character at least 1 slot; distribute the remainder
    // proportionally using the largest-remainder algorithm.
    // Ties in remainder are broken alphabetically (ascending char value).
    const std::size_t n_chars = frequencies.size();
    const std::size_t remaining = total_slots - n_chars;

    std::size_t total_freq = 0;
    for (auto& [ch, cnt] : frequencies) total_freq += cnt;

    // Compute floors and remainders for the proportional share of `remaining`
    struct Entry { char ch; std::size_t floor_share; double remainder; };
    std::vector<Entry> entries;
    entries.reserve(n_chars);
    std::size_t floor_sum = 0;
    for (auto& [ch, cnt] : frequencies) {
        double share = (total_freq > 0)
            ? static_cast<double>(remaining) * static_cast<double>(cnt) / static_cast<double>(total_freq)
            : static_cast<double>(remaining) / static_cast<double>(n_chars);
        std::size_t fl = static_cast<std::size_t>(share);
        floor_sum += fl;
        entries.push_back({ch, fl, share - static_cast<double>(fl)});
    }

    // Sort descending by remainder, alphabetical on tie
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.remainder != b.remainder) return a.remainder > b.remainder;
        return a.ch < b.ch;
    });

    std::size_t leftover = remaining - floor_sum;
    std::map<char, std::size_t> slots;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::size_t extra = (i < leftover) ? 1 : 0;
        slots[entries[i].ch] = 1 + entries[i].floor_share + extra;
    }
    return {slots, total_slots};
}

std::vector<std::pair<std::string, char>> assign_combos(
    const AllocationResult& alloc, int digits) {
    std::vector<std::pair<std::string, char>> result;
    result.reserve(alloc.total_slots);

    // Emit combos in sequential order, grouped by character (sorted alphabetically)
    std::size_t index = 0;
    for (auto& [ch, n_slots] : alloc.slots) {
        for (std::size_t i = 0; i < n_slots; ++i, ++index) {
            // Format index as zero-padded decimal string of length `digits`
            std::string combo = std::to_string(index);
            if (combo.size() < static_cast<std::size_t>(digits))
                combo = std::string(static_cast<std::size_t>(digits) - combo.size(), '0') + combo;
            result.emplace_back(combo, ch);
        }
    }
    return result;
}

void shuffle_assignments(
    std::vector<std::pair<std::string, char>>& assignments,
    std::optional<uint64_t> seed) {
    std::mt19937_64 rng(seed.has_value() ? *seed : std::random_device{}());
    // Shuffle only the characters; combos stay in sequential order so the
    // output file lists entries as 00, 01, 02, ... with randomized char assignments.
    std::vector<char> chars;
    chars.reserve(assignments.size());
    for (auto& [combo, ch] : assignments) chars.push_back(ch);
    std::shuffle(chars.begin(), chars.end(), rng);
    for (std::size_t i = 0; i < assignments.size(); ++i)
        assignments[i].second = chars[i];
}

void write_mapping(
    const std::vector<std::pair<std::string, char>>& assignments,
    int digits, std::ostream& out) {
    out << "digits_per_char=" << digits << '\n';
    out << "base=10\n";
    for (auto& [combo, ch] : assignments)
        out << combo << ' ' << ch << '\n';
}

std::string read_dict_file(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot open dictionary file: " + path.string());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void write_mapping_file(
    const std::vector<std::pair<std::string, char>>& assignments,
    int digits, const std::filesystem::path& path) {
    std::ofstream f(path);
    if (!f)
        throw std::runtime_error("cannot open output file: " + path.string());
    write_mapping(assignments, digits, f);
}

}  // namespace gen_mapping
