#include "pipeline/Pipeline.hpp"
#include "digit_source/DigitSource.hpp"
#include "digit_source/FileDigitSource.hpp"
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <string>
#include <thread>

// ── InfiniteDigitSource: cycles digits 0-9 indefinitely ──────────────────────

class InfiniteDigitSource : public DigitSource {
public:
    std::size_t read_at(std::size_t offset, uint8_t* buf, std::size_t n) override {
        for (std::size_t i = 0; i < n; ++i)
            buf[i] = static_cast<uint8_t>((offset + i) % 10);
        return n;
    }
    std::size_t next_chunk(uint8_t* buf, std::size_t n) override {
        auto pos = pos_.fetch_add(n);
        return read_at(pos, buf, n);
    }
    void reset() override { pos_.store(0); }
    bool is_finite() const override { return false; }
    std::optional<uint64_t> estimated_length() const override { return std::nullopt; }
    int base() const override { return 10; }
private:
    std::atomic<std::size_t> pos_{0};
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::filesystem::path make_run_dir(const char* name) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(p);
    return p;
}

static std::filesystem::path make_temp_digit_file(const std::string& digits) {
    auto p = std::filesystem::temp_directory_path() / "sentinel_test_digits.txt";
    std::ofstream f(p);
    f << digits;
    return p;
}

// Minimal ParallelConfig for sentinel tests
static Pipeline::ParallelConfig make_cfg(const std::filesystem::path& /*run_dir*/) {
    Pipeline::ParallelConfig cfg;
    cfg.chunk_size     = 1000;
    cfg.digit_threads  = 2;
    cfg.mapper_threads = 1;
    cfg.finder_threads = 1;
    cfg.scanner_threads = 1;
    cfg.digit_q_capacity   = 8;
    cfg.letter_q_capacity  = 8;
    cfg.combo_q_capacity   = 8;
    cfg.phrase_q_capacity  = 8;
    return cfg;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(Sentinel, PipelineRunsToCompletionWithoutSentinelFile) {
    auto run_dir = make_run_dir("sentinel_no_stop");
    // Ensure no stale stop file
    std::filesystem::remove("pi_poetry.stop");

    auto digit_file = make_temp_digit_file("31415926535897932384626433832795");
    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.build();
    HumanReviewScanner scanner;

    Pipeline pipeline(source, mapper, finder, scanner);
    pipeline.run_parallel(run_dir, make_cfg(run_dir));

    EXPECT_TRUE(std::filesystem::exists(run_dir / "results.json"));
    EXPECT_FALSE(std::filesystem::exists("pi_poetry.stop"));
    std::filesystem::remove_all(run_dir);
}

TEST(Sentinel, SentinelFilePresentBeforeRunStopsEarly) {
    auto run_dir = make_run_dir("sentinel_preexist");
    // Write sentinel file before starting
    { std::ofstream f("pi_poetry.stop"); }

    InfiniteDigitSource source;
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.build();
    HumanReviewScanner scanner;

    Pipeline pipeline(source, mapper, finder, scanner);

    auto fut = std::async(std::launch::async, [&] {
        pipeline.run_parallel(run_dir, make_cfg(run_dir));
    });

    // Pipeline should stop quickly since sentinel was already there
    auto status = fut.wait_for(std::chrono::seconds(10));
    ASSERT_EQ(status, std::future_status::ready) << "Pipeline did not stop within 10 s";
    fut.get();  // rethrow any exception

    EXPECT_FALSE(std::filesystem::exists("pi_poetry.stop"))
        << "Sentinel file was not deleted";
    std::filesystem::remove_all(run_dir);
}

TEST(Sentinel, SentinelFileStopsPipelineEarly) {
    auto run_dir = make_run_dir("sentinel_stop_mid");
    std::filesystem::remove("pi_poetry.stop");

    InfiniteDigitSource source;
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.build();
    HumanReviewScanner scanner;

    Pipeline pipeline(source, mapper, finder, scanner);

    auto fut = std::async(std::launch::async, [&] {
        pipeline.run_parallel(run_dir, make_cfg(run_dir));
    });

    // Let the pipeline run briefly, then drop the sentinel file
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    { std::ofstream f("pi_poetry.stop"); }

    auto status = fut.wait_for(std::chrono::seconds(10));
    ASSERT_EQ(status, std::future_status::ready) << "Pipeline did not stop within 10 s";
    fut.get();

    EXPECT_FALSE(std::filesystem::exists("pi_poetry.stop"))
        << "Sentinel file was not deleted after graceful stop";
    std::filesystem::remove_all(run_dir);
}

TEST(Sentinel, SentinelFileIsDeletedAfterPipelineStops) {
    auto run_dir = make_run_dir("sentinel_deletion");
    { std::ofstream f("pi_poetry.stop"); }

    InfiniteDigitSource source;
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.build();
    HumanReviewScanner scanner;

    Pipeline pipeline(source, mapper, finder, scanner);

    auto fut = std::async(std::launch::async, [&] {
        pipeline.run_parallel(run_dir, make_cfg(run_dir));
    });
    fut.wait_for(std::chrono::seconds(10));
    fut.get();

    EXPECT_FALSE(std::filesystem::exists("pi_poetry.stop"))
        << "Sentinel file not deleted after pipeline drained";
    std::filesystem::remove_all(run_dir);
}

TEST(Sentinel, WorksWithFileDigitSource) {
    auto run_dir = make_run_dir("sentinel_file_source");

    // Write sentinel before start — pipeline must detect it and stop; file must be deleted
    { std::ofstream f("pi_poetry.stop"); }

    // Large-ish file source so workers have something to check between chunks
    std::string digits(4000, '3');
    auto digit_file = make_temp_digit_file(digits);
    FileDigitSource source(digit_file.string());
    TwoDigitBlockMapper mapper;
    AhoCorasickCPU finder;
    finder.build();
    HumanReviewScanner scanner;

    Pipeline pipeline(source, mapper, finder, scanner);

    Pipeline::ParallelConfig cfg = make_cfg(run_dir);
    cfg.chunk_size = 200;

    auto fut = std::async(std::launch::async, [&] {
        pipeline.run_parallel(run_dir, cfg);
    });

    auto status = fut.wait_for(std::chrono::seconds(10));
    ASSERT_EQ(status, std::future_status::ready);
    fut.get();

    EXPECT_FALSE(std::filesystem::exists("pi_poetry.stop"))
        << "Sentinel file not deleted when using FileDigitSource";
    std::filesystem::remove(digit_file);
    std::filesystem::remove_all(run_dir);
}
