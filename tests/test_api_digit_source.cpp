#include "digit_source/ApiDigitSource.hpp"
#include <gtest/gtest.h>
#include <httplib.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// ── Local test server fixture ─────────────────────────────────────────────────

// Writes a minimal per-source TOML pointing at http://localhost:<port>.
static std::filesystem::path write_api_config(int port,
                                               int max_per_request = 10) {
    auto p = std::filesystem::temp_directory_path() / "test_api_source.toml";
    std::ofstream f(p);
    f << "[api]\n"
      << "base_url        = \"http://localhost:" << port << "/v1/pi\"\n"
      << "start_param     = \"start\"\n"
      << "count_param     = \"numberOfDigits\"\n"
      << "max_per_request = " << max_per_request << "\n\n"
      << "[response]\n"
      << "digits_json_field = \"content\"\n";
    return p;
}

// Digit at position k in the test stream: k % 10
static std::string make_digit_string(uint64_t start, int count) {
    std::string s;
    s.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        s += static_cast<char>('0' + (start + i) % 10);
    return s;
}

struct ServerFixture {
    httplib::Server svr;
    std::thread     svr_thread;
    int             port{0};

    // request_count tracks how many GET requests the server has received
    std::atomic<int> request_count{0};
    // fail_n: server returns HTTP 500 for the first fail_n requests
    std::atomic<int> fail_n{0};

    ServerFixture() {
        svr.Get("/v1/pi", [this](const httplib::Request& req,
                                  httplib::Response& res) {
            if (fail_n.load() > 0) {
                fail_n.fetch_sub(1);
                res.status = 500;
                res.set_content("error", "text/plain");
                return;
            }
            request_count.fetch_add(1);
            uint64_t start = 0;
            int      count = 1;
            if (req.has_param("start"))
                start = static_cast<uint64_t>(std::stoull(req.get_param_value("start")));
            if (req.has_param("numberOfDigits"))
                count = std::stoi(req.get_param_value("numberOfDigits"));
            std::string digits = make_digit_string(start, count);
            std::string body   = "{\"content\":\"" + digits + "\"}";
            res.set_content(body, "application/json");
        });

        port = svr.bind_to_any_port("localhost");
        svr_thread = std::thread([this] { svr.listen_after_bind(); });
        // Give the server a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ~ServerFixture() {
        svr.stop();
        if (svr_thread.joinable()) svr_thread.join();
    }
};

// ── Group A: Metadata ─────────────────────────────────────────────────────────

TEST(ApiDigitSource_Metadata, FiniteWhenMaxDigitsSet) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port);
    ApiDigitSource src(cfg.string(), 5000);
    EXPECT_TRUE(src.is_finite());
    ASSERT_TRUE(src.estimated_length().has_value());
    EXPECT_EQ(*src.estimated_length(), 5000u);
}

TEST(ApiDigitSource_Metadata, InfiniteWhenMaxDigitsZero) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port);
    ApiDigitSource src(cfg.string(), 0);
    EXPECT_FALSE(src.is_finite());
    EXPECT_FALSE(src.estimated_length().has_value());
}

TEST(ApiDigitSource_Metadata, BaseIsTen) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port);
    ApiDigitSource src(cfg.string(), 0);
    EXPECT_EQ(src.base(), 10);
}

// ── Group B: read_at() happy path ─────────────────────────────────────────────

TEST(ApiDigitSource_ReadAt, SingleRequest) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf[5] = {};
    std::size_t n = src.read_at(0, buf, 5);
    ASSERT_EQ(n, 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(buf[i], static_cast<uint8_t>(i % 10)) << "at index " << i;
    EXPECT_EQ(fix.request_count.load(), 1);
}

TEST(ApiDigitSource_ReadAt, OffsetMidStream) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf[3] = {};
    std::size_t n = src.read_at(1000, buf, 3);
    ASSERT_EQ(n, 3u);
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(buf[i], static_cast<uint8_t>((1000 + i) % 10)) << "at " << i;
}

TEST(ApiDigitSource_ReadAt, SpansMultipleRequests) {
    // max_per_request=3, request 7 digits → ceil(7/3)=3 HTTP calls
    ServerFixture fix;
    auto cfg = write_api_config(fix.port, /*max_per_request=*/3);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf[7] = {};
    std::size_t n = src.read_at(0, buf, 7);
    ASSERT_EQ(n, 7u);
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(buf[i], static_cast<uint8_t>(i % 10)) << "at " << i;
    EXPECT_EQ(fix.request_count.load(), 3);
}

TEST(ApiDigitSource_ReadAt, ConcurrentCallsAreIndependent) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    std::vector<std::thread> threads;
    std::vector<std::vector<uint8_t>> results(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            uint64_t offset = static_cast<uint64_t>(t) * 100;
            results[t].resize(5);
            src.read_at(offset, results[t].data(), 5);
        });
    }
    for (auto& th : threads) th.join();

    for (int t = 0; t < 4; ++t) {
        uint64_t offset = static_cast<uint64_t>(t) * 100;
        for (int i = 0; i < 5; ++i)
            EXPECT_EQ(results[t][i], static_cast<uint8_t>((offset + i) % 10))
                << "thread " << t << " index " << i;
    }
}

// ── Group C: next_chunk() and reset() ────────────────────────────────────────

TEST(ApiDigitSource_Sequential, NextChunkFillsBuffer) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf0[5] = {}, buf1[5] = {};
    ASSERT_EQ(src.next_chunk(buf0, 5), 5u);
    ASSERT_EQ(src.next_chunk(buf1, 5), 5u);

    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(buf0[i], static_cast<uint8_t>(i % 10));
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(buf1[i], static_cast<uint8_t>((5 + i) % 10));
}

TEST(ApiDigitSource_Sequential, ResetRestartsFromZero) {
    ServerFixture fix;
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf0[5] = {}, buf1[5] = {};
    src.next_chunk(buf0, 5);
    src.reset();
    src.next_chunk(buf1, 5);

    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(buf0[i], buf1[i]) << "at " << i;
}

// ── Group D: Retry logic ──────────────────────────────────────────────────────

TEST(ApiDigitSource_Retry, SucceedsAfterOneFailure) {
    ServerFixture fix;
    fix.fail_n.store(1);
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf[3] = {};
    EXPECT_NO_THROW(src.read_at(0, buf, 3));
    EXPECT_EQ(buf[0], 0u);
}

TEST(ApiDigitSource_Retry, SucceedsAfterTwoFailures) {
    ServerFixture fix;
    fix.fail_n.store(2);
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf[3] = {};
    EXPECT_NO_THROW(src.read_at(0, buf, 3));
    EXPECT_EQ(buf[0], 0u);
}

TEST(ApiDigitSource_Retry, ThrowsAfterThreeFailures) {
    ServerFixture fix;
    fix.fail_n.store(100);  // always fail
    auto cfg = write_api_config(fix.port, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf[3] = {};
    EXPECT_THROW(src.read_at(0, buf, 3), std::runtime_error);
}

TEST(ApiDigitSource_Retry, ThrowsOnConnectionRefused) {
    // Point at a port with no server
    auto cfg = write_api_config(/*port=*/1, /*max_per_request=*/10);
    ApiDigitSource src(cfg.string(), 0);

    uint8_t buf[3] = {};
    EXPECT_THROW(src.read_at(0, buf, 3), std::runtime_error);
}

// ── Group E: Config parsing ───────────────────────────────────────────────────

TEST(ApiDigitSource_Config, MissingApiTableThrowsOnConstruction) {
    auto p = std::filesystem::temp_directory_path() / "bad_api_source.toml";
    { std::ofstream f(p); f << "[response]\ndigits_json_field = \"content\"\n"; }
    EXPECT_THROW(ApiDigitSource src(p.string(), 0), std::runtime_error);
}

TEST(ApiDigitSource_Config, WrongJsonFieldThrowsOnRead) {
    ServerFixture fix;
    // Override handler to return wrong field name
    fix.svr.Get("/v1/wrong", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"data\":\"123\"}", "application/json");
    });

    auto p = std::filesystem::temp_directory_path() / "wrong_field_api.toml";
    {
        std::ofstream f(p);
        f << "[api]\n"
          << "base_url        = \"http://localhost:" << fix.port << "/v1/wrong\"\n"
          << "start_param     = \"start\"\n"
          << "count_param     = \"numberOfDigits\"\n"
          << "max_per_request = 10\n\n"
          << "[response]\n"
          << "digits_json_field = \"content\"\n";
    }
    ApiDigitSource src(p.string(), 0);
    uint8_t buf[3] = {};
    EXPECT_THROW(src.read_at(0, buf, 3), std::runtime_error);
}
