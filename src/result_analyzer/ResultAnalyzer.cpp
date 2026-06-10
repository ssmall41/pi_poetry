#include "result_analyzer/ResultAnalyzer.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

namespace result_analyzer {

void analyze(const std::filesystem::path& output_dir) {
    auto json_path = output_dir / "results.json";
    if (!std::filesystem::exists(json_path)) {
        throw std::runtime_error("results.json not found in: " + output_dir.string());
    }

    std::ifstream f(json_path);
    if (!f) {
        throw std::runtime_error("Cannot open: " + json_path.string());
    }

    PhraseStatsAccumulator stats;
    PhraseFileWriter writer(output_dir);
    PhraseStreamHandler handler([&](ParsedPhrase&& p) {
        stats.add_phrase(p.start_offset, p.words);
        writer.write_phrase(p.start_offset, p.words);
    });

    nlohmann::json::sax_parse(f, &handler);

    if (!handler.saw_phrases_array()) {
        throw std::runtime_error("results.json missing required key 'phrases'");
    }

    std::ofstream out_stats(output_dir / "statistics.txt");
    if (!out_stats) {
        throw std::runtime_error("Cannot open statistics.txt");
    }
    write_statistics(stats, out_stats);
}

void PhraseStatsAccumulator::add_phrase(std::size_t start_offset,
                                         const std::vector<std::string>& words) {
    length_counts_[words.size()]++;

    std::size_t offset = start_offset;
    for (const auto& w : words) {
        auto it = min_offset_per_word_.find(w);
        if (it == min_offset_per_word_.end()) {
            min_offset_per_word_.emplace(w, offset);
        } else if (offset < it->second) {
            it->second = offset;
        }
        offset += w.size();
    }
}

const std::map<std::size_t, std::size_t>& PhraseStatsAccumulator::length_counts() const {
    return length_counts_;
}

std::vector<WordOccurrence> PhraseStatsAccumulator::top_n_longest_words(std::size_t n) const {
    std::vector<WordOccurrence> all;
    all.reserve(min_offset_per_word_.size());
    for (const auto& [word, offset] : min_offset_per_word_)
        all.push_back({word, offset});

    auto cmp = [](const WordOccurrence& a, const WordOccurrence& b) {
        if (a.word.size() != b.word.size())
            return a.word.size() > b.word.size();
        if (a.offset != b.offset)
            return a.offset < b.offset;
        return a.word < b.word;
    };

    std::size_t k = std::min(n, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(k), all.end(), cmp);
    all.resize(k);
    return all;
}

void write_statistics(const PhraseStatsAccumulator& stats, std::ostream& out) {
    out << "Phrase counts by length:\n";
    for (const auto& [len, cnt] : stats.length_counts())
        out << "  length " << len << ": " << cnt << '\n';

    out << '\n' << "Top 10 longest word occurrences:\n";
    for (const auto& wo : stats.top_n_longest_words(10))
        out << "  " << wo.word << " at offset " << wo.offset
            << " (length " << wo.word.size() << ")\n";
}

PhraseFileWriter::PhraseFileWriter(std::filesystem::path output_dir)
    : output_dir_(std::move(output_dir)) {}

void PhraseFileWriter::write_phrase(std::size_t start_offset,
                                     const std::vector<std::string>& words) {
    std::size_t len = words.size();
    auto it = files_by_length_.find(len);
    if (it == files_by_length_.end()) {
        auto filename = "phrases_length_" + std::to_string(len) + ".txt";
        std::ofstream out(output_dir_ / filename);
        if (!out) {
            throw std::runtime_error("Cannot open output file: " + filename);
        }
        it = files_by_length_.emplace(len, std::move(out)).first;
    }

    auto& out = it->second;
    out << start_offset << ":";
    for (const auto& w : words) out << ' ' << w;
    out << '\n';
}

PhraseStreamHandler::PhraseStreamHandler(PhraseCallback on_phrase)
    : on_phrase_(std::move(on_phrase)) {}

bool PhraseStreamHandler::null() { return true; }

bool PhraseStreamHandler::boolean(bool /*val*/) { return true; }

bool PhraseStreamHandler::number_integer(number_integer_t /*val*/) { return true; }

bool PhraseStreamHandler::number_unsigned(number_unsigned_t val) {
    if (in_phrase_object_ && skip_depth_ == -1 && current_key_ == "start_offset") {
        current_.start_offset = static_cast<std::size_t>(val);
    }
    current_key_.clear();
    return true;
}

bool PhraseStreamHandler::number_float(number_float_t /*val*/, const std::string& /*s*/) {
    return true;
}

bool PhraseStreamHandler::string(std::string& val) {
    if (in_words_array_ && skip_depth_ == -1) {
        current_.words.push_back(std::move(val));
    } else {
        current_key_.clear();
    }
    return true;
}

bool PhraseStreamHandler::binary(nlohmann::json::binary_t& /*val*/) { return true; }

bool PhraseStreamHandler::start_object(std::size_t /*elements*/) {
    ++depth_;
    if (in_phrases_array_ && depth_ == 3 && skip_depth_ == -1) {
        in_phrase_object_ = true;
        current_ = ParsedPhrase{};
    } else if (skip_depth_ == -1 && in_phrase_object_) {
        skip_depth_ = depth_;
    }
    return true;
}

bool PhraseStreamHandler::key(std::string& val) {
    if (in_phrase_object_ && skip_depth_ == -1) {
        current_key_ = val;
    } else if (depth_ == 1) {
        current_key_ = val;
    }
    return true;
}

bool PhraseStreamHandler::end_object() {
    if (skip_depth_ != -1) {
        if (depth_ == skip_depth_) skip_depth_ = -1;
        --depth_;
        return true;
    }
    if (in_phrase_object_ && depth_ == 3) {
        on_phrase_(std::move(current_));
        in_phrase_object_ = false;
    }
    --depth_;
    return true;
}

bool PhraseStreamHandler::start_array(std::size_t /*elements*/) {
    ++depth_;
    if (depth_ == 2 && current_key_ == "phrases" && !in_phrase_object_) {
        in_phrases_array_ = true;
        saw_phrases_ = true;
        current_key_.clear();
    } else if (in_phrase_object_ && depth_ == 4 && current_key_ == "words" && skip_depth_ == -1) {
        in_words_array_ = true;
        current_key_.clear();
    } else if (skip_depth_ == -1 && in_phrase_object_) {
        skip_depth_ = depth_;
    }
    return true;
}

bool PhraseStreamHandler::end_array() {
    if (skip_depth_ != -1) {
        if (depth_ == skip_depth_) skip_depth_ = -1;
        --depth_;
        return true;
    }
    if (in_words_array_ && depth_ == 4) {
        in_words_array_ = false;
    }
    if (in_phrases_array_ && depth_ == 2) {
        in_phrases_array_ = false;
    }
    --depth_;
    return true;
}

bool PhraseStreamHandler::parse_error(std::size_t /*position*/, const std::string& /*last_token*/,
                                       const nlohmann::detail::exception& ex) {
    throw ex;
}

bool PhraseStreamHandler::saw_phrases_array() const { return saw_phrases_; }

}  // namespace result_analyzer
