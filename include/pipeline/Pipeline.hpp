#pragma once
#include "digit_source/DigitSource.hpp"
#include "digit_mapper/DigitMapper.hpp"
#include "word_finder/WordFinder.hpp"
#include "phrase_scanner/PhraseScanner.hpp"
#include <cstddef>
#include <filesystem>
#include <string>

class Pipeline {
public:
    Pipeline(DigitSource& source, DigitMapper& mapper,
             WordFinder& finder, PhraseScanner& scanner);

    // Validates base() == required_base(), then runs all 4 stages serially.
    // Throws std::runtime_error on base mismatch or I/O failure.
    // chunk_size: number of digits read per pass (snapped up to digits_per_char multiple).
    void run(const std::filesystem::path& run_dir, bool write_letter_sequence = false,
             std::size_t chunk_size = 131072);

    struct ParallelConfig {
        std::size_t chunk_size       = 131072;
        bool        write_letters    = false;
        int         digit_threads    = 1;
        int         mapper_threads   = 1;
        int         finder_threads   = 1;
        int         scanner_threads  = 1;
        // Bounded-queue capacity per stage pair. push() blocks when full,
        // providing back-pressure from slow downstream stages.
        std::size_t queue_capacity   = 16;
        bool        debug            = false;
    };

    // Parallel version: each stage runs with cfg.*_threads worker threads.
    // Stages communicate via bounded queues; a reorder buffer ensures
    // results.txt and results.json are written in offset order.
    void run_parallel(const std::filesystem::path& run_dir, const ParallelConfig& cfg);

private:
    DigitSource&   source_;
    DigitMapper&   mapper_;
    WordFinder&    finder_;
    PhraseScanner& scanner_;
};
