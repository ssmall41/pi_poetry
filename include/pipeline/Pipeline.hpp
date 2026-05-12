#pragma once
#include "digit_source/DigitSource.hpp"
#include "digit_mapper/DigitMapper.hpp"
#include "word_finder/WordFinder.hpp"
#include "phrase_scanner/PhraseScanner.hpp"
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

private:
    DigitSource&   source_;
    DigitMapper&   mapper_;
    WordFinder&    finder_;
    PhraseScanner& scanner_;
};
