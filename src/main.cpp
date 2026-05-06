#include "digit_source/FileDigitSource.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include "pipeline/Pipeline.hpp"
#include <toml++/toml.hpp>
#include <chrono>
#include <ctime>
#include <filesystem>
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
        int max_gap = config["phrase_scanner"]["max_gap"].value_or(5);
        std::size_t min_word_length = config["word_finder"]["min_word_length"].value_or(
            std::size_t{1});
        std::string out_dir = config["output"]["dir"].value_or(std::string("outputs"));

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_ptr = std::localtime(&t);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", tm_ptr);
        std::filesystem::path run_dir =
            std::filesystem::path(out_dir) / ("run-" + std::string(buf));
        std::filesystem::create_directories(run_dir);

        std::cout << "Run output: " << run_dir.string() << "\n";

        FileDigitSource source(digit_path);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_min_word_length(min_word_length);
        finder.load_dictionary(dict_path);
        finder.build();
        HumanReviewScanner scanner(max_gap);

        Pipeline pipeline(source, mapper, finder, scanner);
        pipeline.run(run_dir);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
