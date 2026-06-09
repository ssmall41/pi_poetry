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

    {
        std::string mode = config["pipeline"]["mode"].value_or(std::string("serial"));
        if (mode != "serial" && mode != "parallel")
            errors.push_back("pipeline.mode: must be \"serial\" or \"parallel\", got \"" + mode + "\"");
    }
    {
        std::string dtype = config["digit_source"]["type"].value_or(std::string("file"));
        if (dtype != "file" && dtype != "api")
            errors.push_back("digit_source.type: must be \"file\" or \"api\", got \""
                             + dtype + "\"");

        if (dtype == "file") {
            std::string path = config["digit_source"]["path"]
                                   .value_or(std::string("data/pi_2000.txt"));
            if (!std::filesystem::exists(path))
                errors.push_back("digit_source.path: file not found: \"" + path + "\"");
        }
        if (dtype == "api") {
            std::string sc = config["digit_source"]["source_config"].value_or(std::string(""));
            if (sc.empty())
                errors.push_back("digit_source.source_config: required when type is \"api\"");
            else if (!std::filesystem::exists(sc))
                errors.push_back("digit_source.source_config: file not found: \"" + sc + "\"");
        }
        auto md = config["digit_source"]["max_digits"].value<int64_t>();
        if (md && *md < 0)
            errors.push_back("digit_source.max_digits: must be >= 0");
    }
    {
        std::string mtype = config["digit_mapper"]["type"].value_or(std::string("two-digit-block"));
        if (mtype != "two-digit-block" && mtype != "mapping-file")
            errors.push_back("digit_mapper.type: must be \"two-digit-block\" or \"mapping-file\", got \""
                             + mtype + "\"");
        if (mtype == "two-digit-block") {
            check_reserved_str("digit_mapper", "alphabet", "alpha-lower");
        }
        if (mtype == "mapping-file") {
            std::string mf = config["digit_mapper"]["mapping_file"].value_or(std::string(""));
            if (mf.empty())
                errors.push_back("digit_mapper.mapping_file: required when type is \"mapping-file\"");
            else if (!std::filesystem::exists(mf))
                errors.push_back("digit_mapper.mapping_file: file not found: \"" + mf + "\"");
        }
    }
    check_reserved_str("word_finder",   "type",     "aho-corasick-cpu");
    check_reserved_str("phrase_scanner","type",     "human-review");

    auto check_threads = [&](std::string_view section) {
        auto v = config[section]["threads"].value<int64_t>();
        if (v && *v < 1)
            errors.push_back(std::string(section) + ".threads: must be >= 1, got " +
                             std::to_string(*v));
        else if (!v && config[section]["threads"])
            errors.push_back(std::string(section) + ".threads: must be a positive integer");
    };

    check_threads("digit_source");

    {
        auto v = config["digit_source"]["chunk_size"].value<int64_t>();
        if (v && *v < 1)
            errors.push_back("digit_source.chunk_size: must be >= 1, got " +
                             std::to_string(*v));
        else if (!v && config["digit_source"]["chunk_size"])
            errors.push_back("digit_source.chunk_size: must be a positive integer");
    }
    {
        std::string mtype = config["digit_mapper"]["type"].value_or(std::string("two-digit-block"));
        if (mtype == "two-digit-block")
            check_reserved_int("digit_mapper", "base", 10);
    }
    check_threads("digit_mapper");
    check_threads("word_finder");
    check_threads("phrase_scanner");

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
        auto v = config["phrase_scanner"]["min_phrase_length"].value<int64_t>();
        if (v && *v < 1)
            errors.push_back("phrase_scanner.min_phrase_length: must be >= 1, got " +
                             std::to_string(*v));
        else if (!v && config["phrase_scanner"]["min_phrase_length"])
            errors.push_back("phrase_scanner.min_phrase_length: must be an integer >= 1");
    }

    {
        std::string path = config["word_finder"]["dictionary"]
                               .value_or(std::string("dictionaries/english.txt"));
        if (!std::filesystem::exists(path))
            errors.push_back("word_finder.dictionary: file not found: \"" + path + "\"");
    }

    {
        auto node = config["analysis"]["run_after_pipeline"];
        if (node && !node.as_boolean())
            errors.push_back("analysis.run_after_pipeline: must be a boolean");
    }

    return errors;
}
