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
        HumanReviewScanner scanner(5);
        Pipeline{source, mapper, finder, scanner}.run(serial_dir);
    }

    // Parallel run (2 threads per stage)
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner(5);
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

TEST(ParallelPipeline, WordFoundAcrossChunkBoundary) {
    // "000102" → "abc" (3 chars), chunk_size=2 digits = 1 char per chunk.
    // Word "abc" spans all three chunks; overlap buffer must catch it.
    auto digit_file = make_temp_digit_file("000102", "pp_boundary_digits.txt");
    auto run_dir    = std::filesystem::temp_directory_path() / "pp_boundary_run";
    std::filesystem::create_directories(run_dir);

    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.insert_word("abc");
    finder.build();
    HumanReviewScanner scanner(0);

    Pipeline::ParallelConfig cfg;
    // chunk_size=4 digits = 2 chars per chunk. Word "abc" (len 3) spans chunk 0
    // ("ab") into chunk 1 ("c"). The WF lookahead buffer appends up to
    // max_word_len-1=2 chars from chunk 1 to chunk 0's WFInput, giving "abc".
    cfg.chunk_size      = 4;
    cfg.digit_threads   = 1;
    cfg.mapper_threads  = 1;
    cfg.finder_threads  = 2;
    cfg.scanner_threads = 1;
    Pipeline{source, mapper, finder, scanner}.run_parallel(run_dir, cfg);

    std::ifstream f(run_dir / "results.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    std::filesystem::remove_all(run_dir);
    std::filesystem::remove(digit_file);

    EXPECT_NE(contents.find("abc"), std::string::npos) << "contents: " << contents;
}

// ── Phrase spanning a chunk boundary is merged ───────────────────────────────

TEST(ParallelPipeline, PhraseMergedAcrossChunkBoundary) {
    // "whaleof" = 7 chars = 14 digits.
    // "whale"=5 chars, "of"=2 chars, gap=0 → should be one phrase.
    // Using chunk_size=10 digits (5 chars), "whale" is in chunk 0, "of" is in chunk 1.
    // With max_gap=0 and phrase buffer, they must still merge.
    //
    // Digits: whale → chars 'w'=22→"44", 'h'=7→"07", 'a'=0→"00", 'l'=11→"11", 'e'=4→"04"
    // Actually let's compute: TwoDigitBlockMapper: (d[0]*10+d[1]) % 26
    // We need: 'w'=22, 'h'=7, 'a'=0, 'l'=11, 'e'=4, 'o'=14, 'f'=5
    // For 'w'=22: d0*10+d1=22 → "22"
    // For 'h'=7:  "07"
    // For 'a'=0:  "00"
    // For 'l'=11: "11"
    // For 'e'=4:  "04"
    // For 'o'=14: "14"
    // For 'f'=5:  "05"
    // Sequence: "22070011041405"
    auto digit_file = make_temp_digit_file("22070011041405", "pp_phrase_boundary_digits.txt");
    auto run_dir    = std::filesystem::temp_directory_path() / "pp_phrase_boundary_run";
    std::filesystem::create_directories(run_dir);

    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.insert_word("whale");
    finder.insert_word("of");
    finder.build();
    HumanReviewScanner scanner(0);  // max_gap=0: only consecutive words

    Pipeline::ParallelConfig cfg;
    cfg.chunk_size      = 10;  // 5 chars per chunk: chunk0="whale", chunk1="of"
    cfg.digit_threads   = 1;
    cfg.mapper_threads  = 1;
    cfg.finder_threads  = 1;
    cfg.scanner_threads = 1;
    Pipeline{source, mapper, finder, scanner}.run_parallel(run_dir, cfg);

    std::ifstream f(run_dir / "results.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    std::filesystem::remove_all(run_dir);
    std::filesystem::remove(digit_file);

    // Consecutive words spanning a chunk boundary must still be merged into
    // one phrase, just as in the serial pipeline.
    EXPECT_NE(contents.find("whale of"), std::string::npos)
        << "Expected merged phrase 'whale of', got:\n" << contents;
}

// ── Parallel output exactly matches serial across many chunk boundaries ───────
// chunk_size=100 digits = 50 chars/chunk on pi_2000.txt (~20 boundaries).
// Tests that phrase merging, word selection, and AllCombos chains are all
// identical to the serial pipeline regardless of where boundaries fall.

TEST(ParallelPipeline, ParallelMatchesSerial_ETL_MultiChunk) {
    const std::string pi   = PI_POETRY_SOURCE_DIR "/data/pi_2000.txt";
    const std::string dict = PI_POETRY_SOURCE_DIR "/dictionaries/english_trimmed.txt";

    auto serial_dir   = std::filesystem::temp_directory_path() / "pp_serial_etl_nd";
    auto parallel_dir = std::filesystem::temp_directory_path() / "pp_parallel_etl_nd";
    std::filesystem::create_directories(serial_dir);
    std::filesystem::create_directories(parallel_dir);

    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner(5);
        Pipeline{source, mapper, finder, scanner}.run(serial_dir);
    }
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner(5);
        Pipeline::ParallelConfig cfg;
        cfg.chunk_size      = 100;  // small: ~50 chars/chunk, many boundaries
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

    EXPECT_EQ(serial_phrases, parallel_phrases);
}

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
        HumanReviewScanner scanner(5);
        Pipeline{source, mapper, finder, scanner}.run(serial_dir);
    }
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner(5);
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

    EXPECT_EQ(serial_phrases, parallel_phrases);
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
    HumanReviewScanner scanner(0);

    Pipeline::ParallelConfig cfg;
    cfg.chunk_size      = 4;
    cfg.digit_threads   = 1;
    cfg.mapper_threads  = 1;
    cfg.finder_threads  = 1;
    cfg.scanner_threads = 2;
    Pipeline{source, mapper, finder, scanner}.run_parallel(run_dir, cfg);

    std::ifstream f(run_dir / "results.txt");
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    std::filesystem::remove_all(run_dir);
    std::filesystem::remove(digit_file);

    EXPECT_NE(contents.find("Offset 0: ab\n"),  std::string::npos) << contents;
    EXPECT_NE(contents.find("Offset 0: a "),    std::string::npos) << contents;
    EXPECT_NE(contents.find("Offset 1: b\n"),   std::string::npos) << contents;
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
        HumanReviewScanner scanner(5);
        Pipeline{source, mapper, finder, scanner}.run(serial_dir);
    }
    {
        FileDigitSource source(pi);
        TwoDigitBlockMapper mapper;
        AhoCorasickCPU finder;
        finder.set_overlap_policy(OverlapPolicy::AllCombos);
        finder.load_dictionary(dict);
        finder.build();
        HumanReviewScanner scanner(5);
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
