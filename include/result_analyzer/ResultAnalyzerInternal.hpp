#pragma once
// Internal header: the json-dependent SAX machinery for parsing results.json.
// Kept out of the public ResultAnalyzer.hpp so that consumers which only need
// the analyzer API (e.g. main.cpp, analyze_results.cpp) don't transitively
// parse the ~25k-line <nlohmann/json.hpp>. Include this only from
// ResultAnalyzer.cpp and its unit test (test_analyze_results.cpp).
#include "result_analyzer/ResultAnalyzer.hpp"  // ParsedPhrase
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace result_analyzer {

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
