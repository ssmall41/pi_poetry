#pragma once
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace result_analyzer {

struct Phrase {
    std::size_t start_offset{};
    std::vector<std::string> words;
};

struct WordOccurrence {
    std::string word;
    std::size_t offset{};
};

struct AnalysisData {
    std::vector<Phrase> phrases;
};

AnalysisData parse_results_json(const std::string& json_text);

std::vector<WordOccurrence> compute_word_occurrences(const AnalysisData& data);

std::vector<std::size_t> distinct_phrase_lengths(const std::vector<Phrase>& phrases);

void write_phrase_file(const std::vector<Phrase>& phrases,
                       std::size_t length,
                       std::ostream& out);

std::vector<WordOccurrence> top_n_longest_words(
    const std::vector<WordOccurrence>& occurrences,
    std::size_t n = 10);

void write_statistics(const AnalysisData& data, std::ostream& out);

void analyze(const std::filesystem::path& output_dir);

}  // namespace result_analyzer
