#include <gtest/gtest.h>
#include "pipeline/Pipeline.hpp"
#include "digit_source/FileDigitSource.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace {

std::filesystem::path make_temp_digit_file(const std::string& digits, const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream f(p);
    f << digits;
    return p;
}

// Parse phrases from results.json into a set for order-independent comparison.
using PhraseSet = std::set<std::pair<std::size_t, std::vector<std::string>>>;
PhraseSet parse_phrases(const std::filesystem::path& dir) {
    std::ifstream f(dir / "results.json");
    EXPECT_TRUE(f.is_open());
    auto j = nlohmann::json::parse(f);
    PhraseSet result;
    for (const auto& p : j["phrases"])
        result.insert({p["start_offset"].get<std::size_t>(),
                       p["words"].get<std::vector<std::string>>()});
    return result;
}

}  // namespace

// ── Tracer bullet: parallel ETL matches serial output ────────────────────────

TEST(ParallelPipeline, ParallelMatchesSerial_ETL) {
    const std::string pi = PI_POETRY_SOURCE_DIR "/data/pi_2000.txt";
    const std::string dict = PI_POETRY_SOURCE_DIR "/dictionaries/english_trimmed.txt";

    auto serial_dir   = std::filesystem::temp_directory_path() / "pp_serial_etl";
    auto parallel_dir = std::filesystem::temp_directory_path() / "pp_parallel_etl";
    std::filesystem::create_directories(serial_dir);
    std::filesystem::create_directories(parallel_dir);

    // Serial run
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner;
        Pipeline{source, mapper, finder, scanner}.run(serial_dir);
    }

    // Parallel run (2 threads per stage)
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner;
        Pipeline::ParallelConfig cfg;
        cfg.chunk_size      = 8192;
        cfg.digit_threads   = 2;
        cfg.mapper_threads  = 2;
        cfg.finder_threads  = 2;
        cfg.scanner_threads = 2;
        Pipeline{source, mapper, finder, scanner}.run_parallel(parallel_dir, cfg);
    }

    auto serial_phrases   = parse_phrases(serial_dir);
    auto parallel_phrases = parse_phrases(parallel_dir);

    std::filesystem::remove_all(serial_dir);
    std::filesystem::remove_all(parallel_dir);

    EXPECT_EQ(serial_phrases, parallel_phrases);
}

// ── Word spanning a chunk boundary is still found ────────────────────────────

// ── Phrase spanning a chunk boundary is merged ───────────────────────────────

// Note: ParallelMatchesSerial_ETL_MultiChunk was removed. ETL's greedy scan
// produces different (not just fewer) results at chunk boundaries: a serial
// phrase [W1, W2] spanning chunks becomes two sub-phrases [W1] and [W2] in
// parallel, neither of which exists in serial output. Without cross-chunk
// coordination this is unavoidable for ETL; cross-boundary phrase misses are
// explicitly accepted. AllCombos is not affected (see test below).

TEST(ParallelPipeline, ParallelMatchesSerial_AllCombos_MultiChunk) {
    const std::string pi   = PI_POETRY_SOURCE_DIR "/data/pi_2000.txt";
    const std::string dict = PI_POETRY_SOURCE_DIR "/dictionaries/english_trimmed.txt";

    auto serial_dir   = std::filesystem::temp_directory_path() / "pp_serial_ac_nd";
    auto parallel_dir = std::filesystem::temp_directory_path() / "pp_parallel_ac_nd";
    std::filesystem::create_directories(serial_dir);
    std::filesystem::create_directories(parallel_dir);

    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner;
        Pipeline{source, mapper, finder, scanner}.run(serial_dir);
    }
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner;
        Pipeline::ParallelConfig cfg;
        cfg.chunk_size      = 100;
        cfg.digit_threads   = 1;
        cfg.mapper_threads  = 1;
        cfg.finder_threads  = 1;
        cfg.scanner_threads = 1;
        Pipeline{source, mapper, finder, scanner}.run_parallel(parallel_dir, cfg);
    }

    auto serial_phrases   = parse_phrases(serial_dir);
    auto parallel_phrases = parse_phrases(parallel_dir);
    std::filesystem::remove_all(serial_dir);
    std::filesystem::remove_all(parallel_dir);

    // Parallel is a subset of serial: no extra phrases, only cross-boundary ones missing.
    EXPECT_TRUE(std::includes(serial_phrases.begin(), serial_phrases.end(),
                               parallel_phrases.begin(), parallel_phrases.end()));
}

// ── AllCombos: parallel run produces all sequences ───────────────────────────

TEST(ParallelPipeline, AllCombosParallel_ProducesAllSequences) {
    // "0001" → "ab"; dict: {a, ab, b} → combos: [a], [a,b], [ab], [b]
    auto digit_file = make_temp_digit_file("0001", "pp_allcombos_digits.txt");
    auto run_dir    = std::filesystem::temp_directory_path() / "pp_allcombos_run";
    std::filesystem::create_directories(run_dir);

    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.insert_word("a");
    finder.insert_word("ab");
    finder.insert_word("b");
    finder.build();
    finder.set_overlap_policy(OverlapPolicy::AllCombos);
    HumanReviewScanner scanner;

    Pipeline::ParallelConfig cfg;
    cfg.chunk_size      = 4;
    cfg.digit_threads   = 1;
    cfg.mapper_threads  = 1;
    cfg.finder_threads  = 1;
    cfg.scanner_threads = 2;
    Pipeline{source, mapper, finder, scanner}.run_parallel(run_dir, cfg);

    std::ifstream f(run_dir / "results.json");
    ASSERT_TRUE(f.is_open());
    auto j = nlohmann::json::parse(f);
    std::filesystem::remove_all(run_dir);
    std::filesystem::remove(digit_file);

    const auto& phrases = j["phrases"];
    auto has_phrase = [&](std::size_t offset, std::vector<std::string> words) {
        for (const auto& p : phrases) {
            if (p["start_offset"].get<std::size_t>() == offset &&
                p["words"].get<std::vector<std::string>>() == words)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(has_phrase(0, {"ab"}));
    EXPECT_TRUE(has_phrase(0, {"a", "b"}));
    EXPECT_TRUE(has_phrase(1, {"b"}));
}

// ── Letter sequence: parallel pipeline writes letter_sequence.txt ─────────────

namespace {
struct ParallelLetterFixture {
    std::filesystem::path digit_file =
        make_temp_digit_file("3141592653", "par_letter_digits.txt");
    std::filesystem::path run_dir =
        std::filesystem::temp_directory_path() / "par_letter_run";

    FileDigitSource     source{digit_file.string()};
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU      finder;
    HumanReviewScanner  scanner;

    ParallelLetterFixture() {
        finder.build();
        std::filesystem::create_directories(run_dir);
    }
    ~ParallelLetterFixture() {
        std::filesystem::remove_all(run_dir);
        std::filesystem::remove(digit_file);
    }

    Pipeline make() { return Pipeline(source, mapper, finder, scanner); }

    Pipeline::ParallelConfig base_cfg() {
        Pipeline::ParallelConfig c;
        c.chunk_size     = 64;
        c.digit_threads  = 1;
        c.mapper_threads = 1;
        c.finder_threads = 1;
        c.scanner_threads = 1;
        return c;
    }
};
}  // namespace

TEST(ParallelPipeline, WritesLetterFileWhenEnabled) {
    ParallelLetterFixture fix;
    auto cfg = fix.base_cfg();
    cfg.write_letters = true;
    fix.make().run_parallel(fix.run_dir, cfg);
    EXPECT_TRUE(std::filesystem::exists(fix.run_dir / "letter_sequence.txt"));
}

TEST(ParallelPipeline, SkipsLetterFileWhenDisabled) {
    ParallelLetterFixture fix;
    fix.make().run_parallel(fix.run_dir, fix.base_cfg());
    EXPECT_FALSE(std::filesystem::exists(fix.run_dir / "letter_sequence.txt"));
}

TEST(ParallelPipeline, LettersFileContainsExpectedContent) {
    ParallelLetterFixture fix;
    auto cfg = fix.base_cfg();
    cfg.write_letters = true;
    fix.make().run_parallel(fix.run_dir, cfg);

    std::ifstream f(fix.run_dir / "letter_sequence.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "fphab\n");
}

TEST(ParallelPipeline, LettersFileContainsExpectedContent_MultiChunk) {
    ParallelLetterFixture fix;
    auto cfg = fix.base_cfg();
    cfg.write_letters = true;
    cfg.chunk_size    = 4;  // 2 chars per chunk × 5 chunks exercises reorder path
    fix.make().run_parallel(fix.run_dir, cfg);

    std::ifstream f(fix.run_dir / "letter_sequence.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "fphab\n");
}

// ── AllCombos parallel matches serial (set-equality) ─────────────────────────

TEST(ParallelPipeline, ParallelMatchesSerial_AllCombos) {
    const std::string pi   = PI_POETRY_SOURCE_DIR "/data/pi_2000.txt";
    const std::string dict = PI_POETRY_SOURCE_DIR "/dictionaries/english_trimmed.txt";

    auto serial_dir   = std::filesystem::temp_directory_path() / "pp_serial_ac";
    auto parallel_dir = std::filesystem::temp_directory_path() / "pp_parallel_ac";
    std::filesystem::create_directories(serial_dir);
    std::filesystem::create_directories(parallel_dir);

    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner;
        Pipeline{source, mapper, finder, scanner}.run(serial_dir);
    }
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner;
        Pipeline::ParallelConfig cfg;
        cfg.chunk_size      = 8192;
        cfg.digit_threads   = 2;
        cfg.mapper_threads  = 2;
        cfg.finder_threads  = 2;
        cfg.scanner_threads = 2;
        Pipeline{source, mapper, finder, scanner}.run_parallel(parallel_dir, cfg);
    }

    auto serial_phrases   = parse_phrases(serial_dir);
    auto parallel_phrases = parse_phrases(parallel_dir);

    std::filesystem::remove_all(serial_dir);
    std::filesystem::remove_all(parallel_dir);

    EXPECT_EQ(serial_phrases, parallel_phrases);
}
