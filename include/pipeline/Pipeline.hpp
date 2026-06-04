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
        uint64_t    max_digits       = 0;
        bool        write_letters    = false;
        int         digit_threads    = 1;
        int         mapper_threads   = 1;
        int         finder_threads   = 1;
        int         scanner_threads  = 1;
        // Per-queue bounded capacity (listed under the stage that produces into it).
        // push() blocks when full, providing back-pressure from slow downstream stages.
        std::size_t digit_q_capacity   = 16;   // digit_source  → digit_mapper
        std::size_t letter_q_capacity  = 16;   // digit_mapper  → word_finder
        std::size_t combo_q_capacity   = 16;   // word_finder   → phrase_scanner
        std::size_t phrase_q_capacity  = 16;   // phrase_scanner → writer
        bool        debug            = false;
        bool        dry_run          = false;
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
