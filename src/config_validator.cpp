#include "config_validator.hpp"
#include <filesystem>
#include <string>

std::vector<std::string> validate_config(const toml::table& config) {
    std::vector<std::string> errors;

    auto check_reserved_str = [&](std::string_view section, std::string_view key,
                                  std::string_view expected) {
        auto node = config[section][key];
        if (!node) return;
        auto* s = node.as_string();
        if (!s) {
            errors.push_back(std::string(section) + "." + std::string(key) +
                             ": must be \"" + std::string(expected) + "\"");
            return;
        }
        if (s->get() != expected)
            errors.push_back(std::string(section) + "." + std::string(key) +
                             ": must be \"" + std::string(expected) +
                             "\", got \"" + s->get() + "\"");
    };

    auto check_reserved_int = [&](std::string_view section, std::string_view key,
                                  int64_t expected) {
        auto node = config[section][key];
        if (!node) return;
        auto v = node.value<int64_t>();
        if (!v) {
            errors.push_back(std::string(section) + "." + std::string(key) +
                             ": must be " + std::to_string(expected));
            return;
        }
        if (*v != expected)
            errors.push_back(std::string(section) + "." + std::string(key) +
                             ": must be " + std::to_string(expected) +
                             ", got " + std::to_string(*v));
    };

    check_reserved_str("pipeline",      "mode",     "serial");
    check_reserved_str("digit_source",  "type",     "file");
    check_reserved_str("digit_mapper",  "type",     "two-digit-block");
    check_reserved_str("digit_mapper",  "alphabet", "alpha-lower");
    check_reserved_str("word_finder",   "type",     "aho-corasick-cpu");
    check_reserved_str("phrase_scanner","type",     "human-review");
    check_reserved_str("phrase_scanner","mode",     "gap-tolerant");

    check_reserved_int("digit_source",  "threads", 1);
    check_reserved_int("digit_mapper",  "base",    10);
    check_reserved_int("digit_mapper",  "threads", 1);
    check_reserved_int("word_finder",   "threads", 1);
    check_reserved_int("phrase_scanner","threads", 1);

    {
        std::string policy = config["word_finder"]["overlap_policy"]
                                 .value_or(std::string("earliest-then-longest"));
        if (policy != "earliest-then-longest" && policy != "all-combos")
            errors.push_back("word_finder.overlap_policy: must be "
                             "\"earliest-then-longest\" or \"all-combos\""
                             ", got \"" + policy + "\"");
    }

    {
        auto v = config["word_finder"]["min_word_length"].value<int64_t>();
        if (v && *v < 1)
            errors.push_back("word_finder.min_word_length: must be >= 1, got " +
                             std::to_string(*v));
        else if (!v && config["word_finder"]["min_word_length"])
            errors.push_back("word_finder.min_word_length: must be an integer >= 1");
    }

    {
        auto v = config["phrase_scanner"]["max_gap"].value<int64_t>();
        if (v && *v < 0)
            errors.push_back("phrase_scanner.max_gap: must be >= 0, got " +
                             std::to_string(*v));
        else if (!v && config["phrase_scanner"]["max_gap"])
            errors.push_back("phrase_scanner.max_gap: must be an integer >= 0");
    }

    {
        std::string path = config["digit_source"]["path"]
                               .value_or(std::string("data/pi_2000.txt"));
        if (!std::filesystem::exists(path))
            errors.push_back("digit_source.path: file not found: \"" + path + "\"");
    }

    {
        std::string path = config["word_finder"]["dictionary"]
                               .value_or(std::string("dictionaries/english.txt"));
        if (!std::filesystem::exists(path))
            errors.push_back("word_finder.dictionary: file not found: \"" + path + "\"");
    }

    return errors;
}
