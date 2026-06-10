#pragma once
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace result_analyzer {

struct WordOccurrence {
    std::string word;
    std::size_t offset{};
};

void analyze(const std::filesystem::path& output_dir);

// Accumulates phrase statistics in a single pass with memory bounded by the
// number of distinct phrase lengths and the vocabulary size, regardless of
// how many phrases are processed.
class PhraseStatsAccumulator {
public:
    void add_phrase(std::size_t start_offset, const std::vector<std::string>& words);

    const std::map<std::size_t, std::size_t>& length_counts() const;

    // Returns up to n entries sorted by (word length desc, offset asc),
    // one per distinct word at its minimum offset.
    std::vector<WordOccurrence> top_n_longest_words(std::size_t n = 10) const;

private:
    std::map<std::size_t, std::size_t> length_counts_;
    std::unordered_map<std::string, std::size_t> min_offset_per_word_;
};

// Writes phrase-length counts and the top-10 longest word occurrences to
// `out`, in the same format as the legacy write_statistics(AnalysisData&).
void write_statistics(const PhraseStatsAccumulator& stats, std::ostream& out);

// Lazily opens one file per distinct phrase length under output_dir, named
// "phrases_length_<N>.txt", and writes one line per phrase in the format
// "<offset>: <word1> <word2> ...\n".
class PhraseFileWriter {
public:
    explicit PhraseFileWriter(std::filesystem::path output_dir);

    void write_phrase(std::size_t start_offset, const std::vector<std::string>& words);

private:
    std::filesystem::path output_dir_;
    std::map<std::size_t, std::ofstream> files_by_length_;
};

// A single phrase parsed from the "phrases" array, transient: built up by
// PhraseStreamHandler one event at a time and handed off via callback.
struct ParsedPhrase {
    std::size_t start_offset{};
    std::vector<std::string> words;
};

// SAX handler for a results.json document of the form
// {"phrases":[{"start_offset":N,"words":[...], ...other keys ignored...}, ...]}.
// Invokes on_phrase for each completed element of the "phrases" array, then
// discards its state -- memory use does not grow with the number of phrases.
class PhraseStreamHandler : public nlohmann::json_sax<nlohmann::json> {
public:
    using PhraseCallback = std::function<void(ParsedPhrase&&)>;

    explicit PhraseStreamHandler(PhraseCallback on_phrase);

    bool null() override;
    bool boolean(bool val) override;
    bool number_integer(number_integer_t val) override;
    bool number_unsigned(number_unsigned_t val) override;
    bool number_float(number_float_t val, const std::string& s) override;
    bool string(std::string& val) override;
    bool binary(nlohmann::json::binary_t& val) override;
    bool start_object(std::size_t elements) override;
    bool key(std::string& val) override;
    bool end_object() override;
    bool start_array(std::size_t elements) override;
    bool end_array() override;
    bool parse_error(std::size_t position, const std::string& last_token,
                      const nlohmann::detail::exception& ex) override;

    // True once the top-level "phrases" array has been seen.
    bool saw_phrases_array() const;

private:
    PhraseCallback on_phrase_;

    int depth_ = 0;
    bool in_phrases_array_ = false;
    bool in_phrase_object_ = false;
    bool in_words_array_ = false;
    bool saw_phrases_ = false;
    std::string current_key_;
    int skip_depth_ = -1;
    ParsedPhrase current_;
};

}  // namespace result_analyzer
