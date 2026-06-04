#pragma once
#include "DigitSource.hpp"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct ApiConfig {
    std::string base_url;
    std::string start_param;
    std::string count_param;
    int         max_per_request{1000};
    std::string digits_json_field;
};

class ApiDigitSource final : public DigitSource {
public:
    // source_config_path: path to per-source TOML (e.g. config/sources/pi_delivery.toml)
    // max_digits == 0 means indefinite (affects is_finite()/estimated_length() only);
    // the actual read cap is enforced by DigitDispatcher.
    ApiDigitSource(const std::string& source_config_path, uint64_t max_digits);

    std::size_t next_chunk(uint8_t* buffer, std::size_t n) override;
    void reset() override;
    bool is_finite() const override;
    std::optional<uint64_t> estimated_length() const override;
    int base() const override { return 10; }
    std::size_t read_at(std::size_t digit_offset, uint8_t* buffer, std::size_t n) override;

private:
    // Makes one HTTP GET for [start, start+count). Retries up to 3 times with
    // exponential backoff (100 ms, 200 ms). Throws std::runtime_error on failure.
    std::string fetch_range(uint64_t start, int count);

    ApiConfig   cfg_;
    std::string scheme_;       // "http" or "https"
    std::string host_;         // e.g. "api.pi.delivery"
    std::string path_prefix_;  // e.g. "/v1/pi"
    uint64_t    max_digits_{0};
    mutable std::mutex seq_mutex_;
    uint64_t    seq_pos_{0};
};
