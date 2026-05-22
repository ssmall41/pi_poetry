#include "digit_source/FileDigitSource.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include "pipeline/Pipeline.hpp"
#include "result_analyzer/ResultAnalyzer.hpp"
#include "config_validator.hpp"
#include <toml++/toml.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    auto start = std::chrono::steady_clock::now();
    std::string config_path = "config/default.toml";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--config") {
            config_path = argv[i + 1];
        }
    }

    try {
        auto config = toml::parse_file(config_path);

        auto errors = validate_config(config);
        if (!errors.empty()) {
            for (const auto& e : errors)
                std::cerr << "Config error: " << e << "\n";
            return 1;
        }

        std::string digit_path = config["digit_source"]["path"].value_or(
            std::string("data/pi_2000.txt"));
        std::size_t chunk_size = static_cast<std::size_t>(
            config["digit_source"]["chunk_size"].value_or(int64_t{131072}));
        std::string dict_path = config["word_finder"]["dictionary"].value_or(
            std::string("dictionaries/english.txt"));
        int max_gap = config["phrase_scanner"]["max_gap"].value_or(5);
        std::size_t min_word_length = config["word_finder"]["min_word_length"].value_or(
            std::size_t{1});
        std::string out_dir = config["output"]["dir"].value_or(std::string("outputs"));
        bool write_letter_sequence = config["digit_mapper"]["write_letter_sequence"].value_or(false);
        bool run_analysis = config["analysis"]["run_after_pipeline"].value_or(false);
        bool dry_run      = config["pipeline"]["dry_run"].value_or(false);

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_ptr = std::localtime(&t);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", tm_ptr);
        std::filesystem::path run_dir;
        if (!dry_run) {
            run_dir = std::filesystem::path(out_dir) / ("run-" + std::string(buf));
            std::filesystem::create_directories(run_dir);
            std::cout << "Run output: " << run_dir.string() << "\n";
        }

        std::string policy_str = config["word_finder"]["overlap_policy"].value_or(
            std::string("earliest-then-longest"));

        int digit_threads   = config["digit_source"]["threads"].value_or(1);
        int mapper_threads  = config["digit_mapper"]["threads"].value_or(1);
        int finder_threads  = config["word_finder"]["threads"].value_or(1);
        int scanner_threads = config["phrase_scanner"]["threads"].value_or(1);
        bool debug          = config["pipeline"]["debug"].value_or(false);

        FileDigitSource source(digit_path);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_min_word_length(min_word_length);
        if (policy_str == "all-combos")
            finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict_path);
        finder.build();
        HumanReviewScanner scanner(max_gap);

        Pipeline pipeline(source, mapper, finder, scanner);
        Pipeline::ParallelConfig pcfg;
        pcfg.chunk_size      = chunk_size;
        pcfg.write_letters   = write_letter_sequence;
        pcfg.digit_threads   = digit_threads;
        pcfg.mapper_threads  = mapper_threads;
        pcfg.finder_threads  = finder_threads;
        pcfg.scanner_threads = scanner_threads;
        pcfg.debug           = debug;
        pcfg.dry_run         = dry_run;
        pipeline.run_parallel(run_dir, pcfg);
        auto pipeline_end = std::chrono::steady_clock::now();

        if (run_analysis && !dry_run) {
            result_analyzer::analyze(run_dir);
            auto total_end  = std::chrono::steady_clock::now();
            auto pipeline_s = std::chrono::duration<double>(pipeline_end - start).count();
            auto analysis_s = std::chrono::duration<double>(total_end - pipeline_end).count();
            auto total_s    = std::chrono::duration<double>(total_end - start).count();
            std::cout << "Pipeline time:  " << std::fixed << std::setprecision(3) << pipeline_s << "s\n";
            std::cout << "Analysis time:  " << std::fixed << std::setprecision(3) << analysis_s << "s\n";
            std::cout << "Total time:     " << std::fixed << std::setprecision(3) << total_s    << "s\n";
        } else {
            auto elapsed = std::chrono::duration<double>(pipeline_end - start).count();
            std::cout << "Pipeline time:  " << std::fixed << std::setprecision(3) << elapsed << "s\n";
            std::cout << "Total time:     " << std::fixed << std::setprecision(3) << elapsed << "s\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Total time: " << std::fixed << std::setprecision(3) << elapsed << "s\n";
        return 1;
    }
    return 0;
}
