#include "result_analyzer/ResultAnalyzer.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>
#include <stdexcept>

namespace result_analyzer {

AnalysisData parse_results_json(const std::string& json_text) {
    auto j = nlohmann::json::parse(json_text);
    if (!j.contains("phrases")) {
        throw std::runtime_error("results.json missing required key 'phrases'");
    }
    AnalysisData data;
    for (const auto& entry : j["phrases"]) {
        Phrase p;
        p.start_offset = entry.at("start_offset").get<std::size_t>();
        p.words        = entry.at("words").get<std::vector<std::string>>();
        data.phrases.push_back(std::move(p));
    }
    return data;
}

std::vector<WordOccurrence> compute_word_occurrences(const AnalysisData& data) {
    std::vector<WordOccurrence> result;
    for (const auto& phrase : data.phrases) {
        std::size_t offset = phrase.start_offset;
        for (std::size_t i = 0; i < phrase.words.size(); ++i) {
            result.push_back({phrase.words[i], offset});
            offset += phrase.words[i].size();
        }
    }
    return result;
}

std::vector<std::size_t> distinct_phrase_lengths(const std::vector<Phrase>& phrases) {
    std::set<std::size_t> seen;
    for (const auto& p : phrases) seen.insert(p.words.size());
    return {seen.begin(), seen.end()};
}

void write_phrase_file(const std::vector<Phrase>& phrases,
                       std::size_t length,
                       std::ostream& out) {
    for (const auto& p : phrases) {
        if (p.words.size() != length) continue;
        out << p.start_offset << ":";
        for (const auto& w : p.words) out << ' ' << w;
        out << '\n';
    }
}

std::vector<WordOccurrence> top_n_longest_words(
    const std::vector<WordOccurrence>& occurrences,
    std::size_t n)
{
    std::vector<WordOccurrence> sorted = occurrences;
    std::sort(sorted.begin(), sorted.end(),
        [](const WordOccurrence& a, const WordOccurrence& b) {
            if (a.word.size() != b.word.size())
                return a.word.size() > b.word.size();
            return a.offset < b.offset;
        });
    std::vector<WordOccurrence> result;
    std::unordered_set<std::string> seen;
    for (const auto& wo : sorted) {
        if (seen.insert(wo.word).second) {
            result.push_back(wo);
            if (result.size() == n) break;
        }
    }
    return result;
}

void write_statistics(const AnalysisData& data, std::ostream& out) {
    // Section 1: phrase counts by length
    std::map<std::size_t, std::size_t> counts;
    for (const auto& p : data.phrases) counts[p.words.size()]++;

    out << "Phrase counts by length:\n";
    for (const auto& [len, cnt] : counts)
        out << "  length " << len << ": " << cnt << '\n';

    // Section 2: top-10 longest word occurrences
    auto occ = compute_word_occurrences(data);
    auto top = top_n_longest_words(occ, 10);

    out << '\n' << "Top 10 longest word occurrences:\n";
    for (const auto& wo : top)
        out << "  " << wo.word << " at offset " << wo.offset
            << " (length " << wo.word.size() << ")\n";
}

void analyze(const std::filesystem::path& output_dir) {
    auto json_path = output_dir / "results.json";
    if (!std::filesystem::exists(json_path)) {
        throw std::runtime_error("results.json not found in: " + output_dir.string());
    }

    std::ifstream f(json_path);
    if (!f) {
        throw std::runtime_error("Cannot open: " + json_path.string());
    }
    std::ostringstream buf;
    buf << f.rdbuf();

    auto data = parse_results_json(buf.str());
    auto lengths = distinct_phrase_lengths(data.phrases);

    for (std::size_t len : lengths) {
        auto filename = "phrases_length_" + std::to_string(len) + ".txt";
        std::ofstream out(output_dir / filename);
        if (!out) {
            throw std::runtime_error("Cannot open output file: " + filename);
        }
        write_phrase_file(data.phrases, len, out);
    }

    std::ofstream stats(output_dir / "statistics.txt");
    if (!stats) {
        throw std::runtime_error("Cannot open statistics.txt");
    }
    write_statistics(data, stats);
}

}  // namespace result_analyzer
