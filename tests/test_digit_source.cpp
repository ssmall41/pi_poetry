#include <gtest/gtest.h>
#include "digit_source/FileDigitSource.hpp"
#include "pipeline/DigitDispatcher.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include <vector>

namespace {

std::filesystem::path make_temp_digit_file(const std::string& digits) {
    auto p = std::filesystem::temp_directory_path() / "pi_test_digits.txt";
    std::ofstream f(p);
    f << digits;
    return p;
}

}  // namespace

TEST(FileDigitSource, NextChunkFillsBuffer) {
    auto p = make_temp_digit_file("31415");
    FileDigitSource src(p.string());
    uint8_t buf[5]{};
    auto n = src.next_chunk(buf, 5);
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(buf[0], 3u);
    EXPECT_EQ(buf[1], 1u);
    EXPECT_EQ(buf[2], 4u);
    EXPECT_EQ(buf[3], 1u);
    EXPECT_EQ(buf[4], 5u);
}

TEST(FileDigitSource, ResetReadsFromStart) {
    auto p = make_temp_digit_file("31415");
    FileDigitSource src(p.string());
    uint8_t buf1[3]{}, buf2[3]{};
    src.next_chunk(buf1, 3);
    src.reset();
    src.next_chunk(buf2, 3);
    EXPECT_EQ(buf1[0], buf2[0]);
    EXPECT_EQ(buf1[1], buf2[1]);
    EXPECT_EQ(buf1[2], buf2[2]);
}

TEST(FileDigitSource, Metadata) {
    auto p = make_temp_digit_file("12345");
    FileDigitSource src(p.string());
    EXPECT_TRUE(src.is_finite());
    EXPECT_EQ(src.base(), 10);
}

TEST(FileDigitSource, EndOfStreamPartialRead) {
    auto p = make_temp_digit_file("31415");
    FileDigitSource src(p.string());
    uint8_t buf[10]{};
    auto n = src.next_chunk(buf, 10);
    EXPECT_EQ(n, 5u);
}

TEST(FileDigitSource, EstimatedLength) {
    auto p = make_temp_digit_file("31415");
    FileDigitSource src(p.string());
    EXPECT_EQ(src.estimated_length(), std::optional<uint64_t>{5u});
}

TEST(FileDigitSource, SequentialChunksReadAllDigits) {
    auto p = make_temp_digit_file("1234567890");
    FileDigitSource src(p.string());
    uint8_t buf[5]{};
    EXPECT_EQ(src.next_chunk(buf, 5), 5u);
    EXPECT_EQ(buf[0], 1u); EXPECT_EQ(buf[4], 5u);
    EXPECT_EQ(src.next_chunk(buf, 5), 5u);
    EXPECT_EQ(buf[0], 6u); EXPECT_EQ(buf[4], 0u);
    EXPECT_EQ(src.next_chunk(buf, 5), 0u);  // end of stream
}

// ── read_at tests ─────────────────────────────────────────────────────────────

TEST(FileDigitSource, ReadAtConcurrentCallsAreIndependent) {
    auto p = make_temp_digit_file("1234567890");
    FileDigitSource src(p.string());

    std::vector<std::vector<uint8_t>> results(4, std::vector<uint8_t>(3, 0));
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i] {
            src.read_at(static_cast<std::size_t>(i) * 2, results[i].data(), 3);
        });
    }
    for (auto& t : threads) t.join();

    // Thread i reads digits starting at offset i*2
    // digits: 1 2 3 4 5 6 7 8 9 0
    // offset 0 → 1,2,3  offset 2 → 3,4,5  offset 4 → 5,6,7  offset 6 → 7,8,9
    EXPECT_EQ(results[0][0], 1u); EXPECT_EQ(results[0][2], 3u);
    EXPECT_EQ(results[1][0], 3u); EXPECT_EQ(results[1][2], 5u);
    EXPECT_EQ(results[2][0], 5u); EXPECT_EQ(results[2][2], 7u);
    EXPECT_EQ(results[3][0], 7u); EXPECT_EQ(results[3][2], 9u);
}

TEST(FileDigitSource, ReadAtPastEofReturnsZero) {
    auto p = make_temp_digit_file("31415");
    FileDigitSource src(p.string());
    uint8_t buf[4]{};
    EXPECT_EQ(src.read_at(5, buf, 4), 0u);
}

TEST(FileDigitSource, ReadAtPartialAtEnd) {
    auto p = make_temp_digit_file("31415");
    FileDigitSource src(p.string());
    uint8_t buf[10]{};
    auto n = src.read_at(3, buf, 10);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(buf[0], 1u);
    EXPECT_EQ(buf[1], 5u);
}

TEST(FileDigitSource, ReadAtMidOffsetReturnsCorrectSlice) {
    auto p = make_temp_digit_file("1234567890");
    FileDigitSource src(p.string());
    uint8_t buf[4]{};
    auto n = src.read_at(3, buf, 4);
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(buf[0], 4u);
    EXPECT_EQ(buf[1], 5u);
    EXPECT_EQ(buf[2], 6u);
    EXPECT_EQ(buf[3], 7u);
}

TEST(FileDigitSource, ReadAtOffset0ReturnsCorrectDigits) {
    auto p = make_temp_digit_file("31415");
    FileDigitSource src(p.string());
    uint8_t buf[5]{};
    auto n = src.read_at(0, buf, 5);
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(buf[0], 3u);
    EXPECT_EQ(buf[1], 1u);
    EXPECT_EQ(buf[2], 4u);
    EXPECT_EQ(buf[3], 1u);
    EXPECT_EQ(buf[4], 5u);
}

#ifdef PI_POETRY_SOURCE_DIR
TEST(FileDigitSourceIntegration, ReadsPi2000) {
    const std::string path = PI_POETRY_SOURCE_DIR "/data/pi_2000.txt";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "data/pi_2000.txt not present";
    FileDigitSource src(path);
    EXPECT_EQ(src.estimated_length(), std::optional<uint64_t>{2000u});
    uint8_t buf[5]{};
    src.next_chunk(buf, 5);
    EXPECT_EQ(buf[0], 3u);
    EXPECT_EQ(buf[1], 1u);
    EXPECT_EQ(buf[2], 4u);
    EXPECT_EQ(buf[3], 1u);
    EXPECT_EQ(buf[4], 5u);
    src.reset();
    std::size_t total = 0;
    uint8_t chunk[256]{};
    std::size_t n;
    while ((n = src.next_chunk(chunk, 256)) > 0) {
        total += n;
        for (std::size_t i = 0; i < n; ++i)
            ASSERT_LE(chunk[i], 9u) << "Non-digit at position " << total;
    }
    EXPECT_EQ(total, 2000u);
}
#endif

// ── DigitDispatcher tests ─────────────────────────────────────────────────────

TEST(DigitDispatcher, NextWithoutLookaheadAssignsSeqIdsAndReturnsAllDigits) {
    auto p = make_temp_digit_file("1234567890");
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/5, /*lookahead_digits=*/0);

    auto pkg0 = disp.next();
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->seq_id, 0u);
    EXPECT_EQ(pkg0->global_digit_offset, 0u);
    EXPECT_EQ(pkg0->num_real_digits, 5u);
    EXPECT_EQ(pkg0->digits.size(), 5u);
    EXPECT_EQ(pkg0->digits[0], 1u);
    EXPECT_EQ(pkg0->digits[4], 5u);

    auto pkg1 = disp.next();
    ASSERT_TRUE(pkg1.has_value());
    EXPECT_EQ(pkg1->seq_id, 1u);
    EXPECT_EQ(pkg1->global_digit_offset, 5u);
    EXPECT_EQ(pkg1->num_real_digits, 5u);
    EXPECT_EQ(pkg1->digits[0], 6u);
    EXPECT_EQ(pkg1->digits[4], 0u);

    EXPECT_FALSE(disp.next().has_value());
}

TEST(DigitDispatcher, NextWithLookaheadIncludesExtraDigits) {
    auto p = make_temp_digit_file("1234567890");
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/4, /*lookahead_digits=*/2);

    auto pkg0 = disp.next();
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->num_real_digits, 4u);
    EXPECT_EQ(pkg0->digits.size(), 6u);   // 4 real + 2 lookahead
    EXPECT_EQ(pkg0->digits[0], 1u);
    EXPECT_EQ(pkg0->digits[5], 6u);       // lookahead reaches digit at offset 5

    auto pkg1 = disp.next();
    ASSERT_TRUE(pkg1.has_value());
    EXPECT_EQ(pkg1->global_digit_offset, 4u);
    EXPECT_EQ(pkg1->num_real_digits, 4u);
    EXPECT_EQ(pkg1->digits.size(), 6u);
    EXPECT_EQ(pkg1->digits[0], 5u);       // real starts at offset 4
    EXPECT_EQ(pkg1->digits[5], 0u);       // lookahead: offset 9
}

TEST(DigitDispatcher, LastChunkNumRealDigitsCorrect) {
    auto p = make_temp_digit_file("1234567");  // 7 digits, chunk=4, lookahead=2
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/4, /*lookahead_digits=*/2);

    auto pkg0 = disp.next();   // offsets 0..5 (4 real + 2 lookahead)
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->num_real_digits, 4u);

    auto pkg1 = disp.next();   // offset 4: 3 remaining digits (4+2 wanted, only 3 available)
    ASSERT_TRUE(pkg1.has_value());
    EXPECT_EQ(pkg1->global_digit_offset, 4u);
    EXPECT_EQ(pkg1->num_real_digits, 3u);  // only 3 real digits remain
    EXPECT_EQ(pkg1->digits.size(), 3u);

    EXPECT_FALSE(disp.next().has_value());
}

TEST(DigitDispatcher, ConcurrentNextProducesUniqueSeqIds) {
    // 20 digits, chunk=2, no lookahead → 10 chunks expected
    std::string digits = "12345678901234567890";
    auto p = make_temp_digit_file(digits);
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/2, /*lookahead_digits=*/0);

    std::vector<DigitPackage> collected;
    std::mutex mu;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            while (auto pkg = disp.next()) {
                std::lock_guard<std::mutex> lk(mu);
                collected.push_back(std::move(*pkg));
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(collected.size(), 10u);
    std::set<std::size_t> ids;
    for (auto& pkg : collected) ids.insert(pkg.seq_id);
    EXPECT_EQ(ids.size(), 10u);  // all seq_ids unique
}

// ── DigitDispatcher max_digits cap ───────────────────────────────────────────

TEST(DigitDispatcher_MaxDigits, StopsAtCap) {
    // File has 14 digits; cap at 10 with chunk_size=6 → 2 chunks then stop
    auto p = make_temp_digit_file("12345678901234");
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/6, /*lookahead_digits=*/0, /*max_digits=*/10);

    auto pkg0 = disp.next();
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->seq_id, 0u);
    EXPECT_EQ(pkg0->global_digit_offset, 0u);
    EXPECT_EQ(pkg0->num_real_digits, 6u);

    auto pkg1 = disp.next();
    ASSERT_TRUE(pkg1.has_value());
    EXPECT_EQ(pkg1->seq_id, 1u);
    EXPECT_EQ(pkg1->global_digit_offset, 6u);
    EXPECT_EQ(pkg1->num_real_digits, 4u);  // clamped: 10 - 6 = 4

    EXPECT_FALSE(disp.next().has_value());
}

TEST(DigitDispatcher_MaxDigits, StopsAtCapWithLookahead) {
    // Same as above but with lookahead=2; lookahead doesn't affect num_real_digits
    auto p = make_temp_digit_file("12345678901234");
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/6, /*lookahead_digits=*/2, /*max_digits=*/10);

    auto pkg0 = disp.next();
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->num_real_digits, 6u);
    EXPECT_EQ(pkg0->digits.size(), 8u);    // 6 real + 2 lookahead

    auto pkg1 = disp.next();
    ASSERT_TRUE(pkg1.has_value());
    EXPECT_EQ(pkg1->num_real_digits, 4u);  // clamped real portion

    EXPECT_FALSE(disp.next().has_value());
}

TEST(DigitDispatcher_MaxDigits, ZeroMeansNoCap) {
    auto p = make_temp_digit_file("1234567890");
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/5, /*lookahead_digits=*/0, /*max_digits=*/0);

    auto pkg0 = disp.next();
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->num_real_digits, 5u);

    auto pkg1 = disp.next();
    ASSERT_TRUE(pkg1.has_value());
    EXPECT_EQ(pkg1->num_real_digits, 5u);

    EXPECT_FALSE(disp.next().has_value());
}

TEST(DigitDispatcher_MaxDigits, CapLargerThanSourceRunsToEof) {
    auto p = make_temp_digit_file("1234567890");  // 10 digits
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/5, /*lookahead_digits=*/0, /*max_digits=*/99999);

    auto pkg0 = disp.next();
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->num_real_digits, 5u);

    auto pkg1 = disp.next();
    ASSERT_TRUE(pkg1.has_value());
    EXPECT_EQ(pkg1->num_real_digits, 5u);

    EXPECT_FALSE(disp.next().has_value());
}

TEST(DigitDispatcher_MaxDigits, CapExactlyOnChunkBoundary) {
    // max_digits=6, chunk_size=6 → exactly one chunk, then stop
    auto p = make_temp_digit_file("123456789");
    FileDigitSource src(p.string());
    DigitDispatcher disp(src, /*chunk_size=*/6, /*lookahead_digits=*/0, /*max_digits=*/6);

    auto pkg0 = disp.next();
    ASSERT_TRUE(pkg0.has_value());
    EXPECT_EQ(pkg0->num_real_digits, 6u);

    EXPECT_FALSE(disp.next().has_value());
}
