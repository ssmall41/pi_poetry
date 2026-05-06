#include "digit_source/FileDigitSource.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include "pipeline/Pipeline.hpp"
#include <toml++/toml.hpp>
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    std::string config_path = "config/default.toml";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--config") {
            config_path = argv[i + 1];
        }
    }

    try {
        auto config = toml::parse_file(config_path);

        std::string digit_path = config["digit_source"]["path"].value_or(
            std::string("data/pi_2000.txt"));
        std::string dict_path = config["word_finder"]["dictionary"].value_or(
            std::string("dictionaries/english.txt"));
        std::string out_text = config["phrase_scanner"]["output_text"].value_or(
            std::string("results.txt"));
        std::string out_json = config["phrase_scanner"]["output_json"].value_or(
            std::string("results.json"));
        std::string out_letters = config["digit_mapper"]["output_letters"].value_or(
            std::string(""));
        int max_gap = config["phrase_scanner"]["max_gap"].value_or(5);

        FileDigitSource source(digit_path);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.load_dictionary(dict_path);
        finder.build();
        HumanReviewScanner scanner(max_gap);

        Pipeline pipeline(source, mapper, finder, scanner);
        pipeline.run(out_text, out_json, out_letters);

        std::cout << "Pi Poetry complete. Results: " << out_text
                  << ", " << out_json << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
