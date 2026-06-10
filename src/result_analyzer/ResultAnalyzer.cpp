#include "result_analyzer/ResultAnalyzer.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace result_analyzer {

std::vector<std::pair<std::size_t, std::size_t>> find_phrase_chunk_boundaries(
    const std::filesystem::path& json_path, std::size_t n_threads) {
    if (n_threads < 1) n_threads = 1;

    std::error_code ec;
    auto total_size = std::filesystem::file_size(json_path, ec);
    if (ec) return {};

    std::ifstream f(json_path, std::ios::binary);
    if (!f) return {};

    constexpr std::size_t kPrefixScan = 65536;
    std::size_t prefix_len = std::min<std::size_t>(total_size, kPrefixScan);
    std::string prefix(prefix_len, '\0');
    f.read(prefix.data(), static_cast<std::streamsize>(prefix_len));

    auto key_pos = prefix.find("\"phrases\":");
    if (key_pos == std::string::npos) return {};
    auto bracket_pos = prefix.find('[', key_pos);
    if (bracket_pos == std::string::npos) return {};

    std::size_t array_start = bracket_pos + 1;
    std::size_t span = total_size > array_start ? total_size - array_start : 0;

    std::vector<std::size_t> targets;
    for (std::size_t k = 1; k < n_threads; ++k)
        targets.push_back(array_start + (span * k) / n_threads);

    // "Cuts" mark the gaps (',' separators) between consecutive top-level
    // phrase objects where a target offset falls. Each cut is
    // (end-of-object-before-gap, start-of-object-after-gap); chunk ranges are
    // built from these so that no chunk's byte range includes a separator.
    std::vector<std::pair<std::size_t, std::size_t>> cuts;
    std::size_t next_target = 0;
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    std::size_t object_count = 0;
    std::size_t first_object_start = array_start;
    std::size_t prev_object_end = array_start;
    std::size_t last_object_end = array_start;
    bool finished = false;

    f.clear();
    f.seekg(static_cast<std::streamoff>(array_start));
    constexpr std::size_t kBlockSize = 65536;
    std::vector<char> buf(kBlockSize);
    std::size_t pos = array_start;

    while (!finished && f) {
        f.read(buf.data(), static_cast<std::streamsize>(kBlockSize));
        std::streamsize got = f.gcount();
        if (got <= 0) break;
        for (std::streamsize i = 0; i < got; ++i) {
            char b = buf[static_cast<std::size_t>(i)];
            if (in_string) {
                if (escape) escape = false;
                else if (b == '\\') escape = true;
                else if (b == '"') in_string = false;
                ++pos;
                continue;
            }
            if (b == '"') {
                in_string = true;
                ++pos;
                continue;
            }
            if (b == '{' || b == '[') {
                if (depth == 0) {
                    if (object_count == 0) {
                        first_object_start = pos;
                    } else {
                        while (next_target < targets.size() && pos >= targets[next_target]) {
                            cuts.emplace_back(prev_object_end, pos);
                            ++next_target;
                        }
                    }
                }
                ++depth;
                ++pos;
                continue;
            }
            if (b == '}' || b == ']') {
                if (depth == 0) {
                    finished = true;
                    break;
                }
                --depth;
                if (depth == 0) {
                    prev_object_end = pos + 1;
                    last_object_end = pos + 1;
                    ++object_count;
                }
                ++pos;
                continue;
            }
            ++pos;
        }
    }

    if (object_count == 0) return {};

    // Multiple targets can land in the same gap between two objects; collapse
    // those into a single cut.
    std::vector<std::pair<std::size_t, std::size_t>> dedup_cuts;
    for (const auto& c : cuts)
        if (dedup_cuts.empty() || c != dedup_cuts.back()) dedup_cuts.push_back(c);

    std::vector<std::pair<std::size_t, std::size_t>> result;
    if (dedup_cuts.empty()) {
        result.emplace_back(first_object_start, last_object_end);
    } else {
        result.emplace_back(first_object_start, dedup_cuts.front().first);
        for (std::size_t i = 1; i < dedup_cuts.size(); ++i)
            result.emplace_back(dedup_cuts[i - 1].second, dedup_cuts[i].first);
        result.emplace_back(dedup_cuts.back().second, last_object_end);
    }
    return result;
}

namespace {
constexpr std::size_t kChunkBufferSize = 65536;
constexpr char kPhrasesPrefix[] = "{\"phrases\":[";
constexpr char kPhrasesSuffix[] = "]}";
}  // namespace

PhraseChunkStreamBuf::PhraseChunkStreamBuf(const std::filesystem::path& json_path,
                                            std::size_t start, std::size_t end)
    : file_(json_path, std::ios::binary),
      remaining_(end > start ? end - start : 0),
      buffer_(kChunkBufferSize) {
    file_.seekg(static_cast<std::streamoff>(start));
}

PhraseChunkStreamBuf::int_type PhraseChunkStreamBuf::underflow() {
    if (gptr() < egptr()) return traits_type::to_int_type(*gptr());

    switch (phase_) {
        case Phase::Prefix: {
            constexpr std::size_t n = sizeof(kPhrasesPrefix) - 1;
            std::copy(kPhrasesPrefix, kPhrasesPrefix + n, buffer_.begin());
            setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
            phase_ = Phase::Body;
            return traits_type::to_int_type(*gptr());
        }
        case Phase::Body: {
            if (remaining_ == 0) {
                phase_ = Phase::Suffix;
                return underflow();
            }
            std::size_t to_read = std::min(remaining_, buffer_.size());
            file_.read(buffer_.data(), static_cast<std::streamsize>(to_read));
            std::streamsize got = file_.gcount();
            if (got <= 0) {
                remaining_ = 0;
                phase_ = Phase::Suffix;
                return underflow();
            }
            remaining_ -= static_cast<std::size_t>(got);
            setg(buffer_.data(), buffer_.data(), buffer_.data() + got);
            return traits_type::to_int_type(*gptr());
        }
        case Phase::Suffix: {
            constexpr std::size_t n = sizeof(kPhrasesSuffix) - 1;
            std::copy(kPhrasesSuffix, kPhrasesSuffix + n, buffer_.begin());
            setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
            phase_ = Phase::Done;
            return traits_type::to_int_type(*gptr());
        }
        case Phase::Done:
        default:
            return traits_type::eof();
    }
}

namespace {

void analyze_single_threaded(const std::filesystem::path& output_dir) {
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

// Parses the byte range [chunk_start, chunk_end) of json_path as a phrases
// array, accumulating into stats_out and writing phrase files into part_dir.
void analyze_chunk(const std::filesystem::path& json_path, std::size_t chunk_start,
                    std::size_t chunk_end, const std::filesystem::path& part_dir,
                    PhraseStatsAccumulator& stats_out) {
    std::filesystem::create_directories(part_dir);

    PhraseChunkStreamBuf buf(json_path, chunk_start, chunk_end);
    std::istream in(&buf);

    PhraseFileWriter writer(part_dir);
    PhraseStreamHandler handler([&](ParsedPhrase&& p) {
        stats_out.add_phrase(p.start_offset, p.words);
        writer.write_phrase(p.start_offset, p.words);
    });

    nlohmann::json::sax_parse(in, &handler);
}

// Concatenates output_dir/.analyze_tmp/part_<i>/phrases_length_<L>.txt for
// i in [0, n_parts), in part order, into output_dir/phrases_length_<L>.txt
// for every length L present in any part, then removes the temp directory.
void merge_part_files(const std::filesystem::path& output_dir, std::size_t n_parts) {
    auto tmp_dir = output_dir / ".analyze_tmp";

    std::set<std::size_t> lengths;
    for (std::size_t i = 0; i < n_parts; ++i) {
        auto part_dir = tmp_dir / ("part_" + std::to_string(i));
        if (!std::filesystem::exists(part_dir)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(part_dir)) {
            auto name = entry.path().filename().string();
            constexpr std::string_view prefix = "phrases_length_";
            constexpr std::string_view suffix = ".txt";
            if (name.size() <= prefix.size() + suffix.size()) continue;
            if (!name.starts_with(prefix) || !name.ends_with(suffix)) continue;
            auto len_str = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
            lengths.insert(static_cast<std::size_t>(std::stoul(len_str)));
        }
    }

    for (auto len : lengths) {
        auto filename = "phrases_length_" + std::to_string(len) + ".txt";
        std::ofstream out(output_dir / filename, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Cannot open output file: " + filename);
        }
        for (std::size_t i = 0; i < n_parts; ++i) {
            auto part_file = tmp_dir / ("part_" + std::to_string(i)) / filename;
            if (!std::filesystem::exists(part_file)) continue;
            std::ifstream in(part_file, std::ios::binary);
            out << in.rdbuf();
        }
    }

    std::filesystem::remove_all(tmp_dir);
}

void analyze_parallel(const std::filesystem::path& output_dir,
                       const std::filesystem::path& json_path,
                       const std::vector<std::pair<std::size_t, std::size_t>>& chunks) {
    std::size_t n = chunks.size();
    auto tmp_dir = output_dir / ".analyze_tmp";

    std::vector<PhraseStatsAccumulator> part_stats(n);
    std::vector<std::exception_ptr> errors(n);
    std::vector<std::thread> threads;
    threads.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        threads.emplace_back([&, i] {
            try {
                auto part_dir = tmp_dir / ("part_" + std::to_string(i));
                analyze_chunk(json_path, chunks[i].first, chunks[i].second, part_dir,
                               part_stats[i]);
            } catch (...) {
                errors[i] = std::current_exception();
            }
        });
    }
    for (auto& t : threads) t.join();

    for (const auto& e : errors)
        if (e) std::rethrow_exception(e);

    PhraseStatsAccumulator merged;
    for (const auto& s : part_stats) merged.merge(s);

    std::ofstream out_stats(output_dir / "statistics.txt");
    if (!out_stats) {
        throw std::runtime_error("Cannot open statistics.txt");
    }
    write_statistics(merged, out_stats);

    merge_part_files(output_dir, n);
}

}  // namespace

void analyze(const std::filesystem::path& output_dir, std::size_t n_threads) {
    auto json_path = output_dir / "results.json";

    if (n_threads > 1) {
        auto chunks = find_phrase_chunk_boundaries(json_path, n_threads);
        if (chunks.size() > 1) {
            analyze_parallel(output_dir, json_path, chunks);
            return;
        }
    }

    analyze_single_threaded(output_dir);
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

void PhraseStatsAccumulator::merge(const PhraseStatsAccumulator& other) {
    for (const auto& [len, cnt] : other.length_counts_)
        length_counts_[len] += cnt;

    for (const auto& [word, offset] : other.min_offset_per_word_) {
        auto it = min_offset_per_word_.find(word);
        if (it == min_offset_per_word_.end()) {
            min_offset_per_word_.emplace(word, offset);
        } else if (offset < it->second) {
            it->second = offset;
        }
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
