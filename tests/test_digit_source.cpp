#include <gtest/gtest.h>
#include "digit_source/FileDigitSource.hpp"
#include <filesystem>
#include <fstream>

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
