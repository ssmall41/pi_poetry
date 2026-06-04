#include "digit_source/ApiDigitSource.hpp"
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>
#include <httplib.h>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_map>

// Splits "http://host:port/path" or "https://host/path" into scheme, host, path.
static void parse_url(const std::string& url,
                       std::string& scheme, std::string& host, std::string& path) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos)
        throw std::runtime_error("ApiDigitSource: invalid base_url (missing scheme): " + url);
    scheme = url.substr(0, scheme_end);
    auto rest = url.substr(scheme_end + 3);
    auto slash = rest.find('/');
    if (slash == std::string::npos) {
        host = rest;
        path = "/";
    } else {
        host = rest.substr(0, slash);
        path = rest.substr(slash);
    }
}

ApiDigitSource::ApiDigitSource(const std::string& source_config_path, uint64_t max_digits)
    : max_digits_(max_digits) {
    auto tbl = toml::parse_file(source_config_path);

    auto api = tbl["api"].as_table();
    if (!api)
        throw std::runtime_error("ApiDigitSource: missing [api] table in " + source_config_path);

    auto get_str = [&](const toml::table& t, const char* key) -> std::string {
        auto v = t[key].value<std::string>();
        if (!v) throw std::runtime_error(
            std::string("ApiDigitSource: missing required field api.") + key);
        return *v;
    };

    cfg_.base_url          = get_str(*api, "base_url");
    cfg_.start_param       = get_str(*api, "start_param");
    cfg_.count_param       = get_str(*api, "count_param");
    cfg_.max_per_request   = api->get("max_per_request")
                                  ? api->at("max_per_request").value_or(1000) : 1000;

    auto resp = tbl["response"].as_table();
    if (!resp)
        throw std::runtime_error("ApiDigitSource: missing [response] table in " + source_config_path);
    cfg_.digits_json_field = get_str(*resp, "digits_json_field");

    parse_url(cfg_.base_url, scheme_, host_, path_prefix_);
}

std::string ApiDigitSource::fetch_range(uint64_t start, int count) {
    // One persistent connection per calling thread per host — eliminates repeated
    // TLS handshakes and the thread churn they cause.
    static thread_local std::unordered_map<std::string,
        std::unique_ptr<httplib::SSLClient>> tl_ssl;
    static thread_local std::unordered_map<std::string,
        std::unique_ptr<httplib::Client>>    tl_http;

    std::string query = path_prefix_
        + "?" + cfg_.start_param + "=" + std::to_string(start)
        + "&" + cfg_.count_param + "=" + std::to_string(count);

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100 << (attempt - 1)));

        httplib::Result res;
        if (scheme_ == "https") {
            auto& cli = tl_ssl[host_];
            if (!cli) {
                cli = std::make_unique<httplib::SSLClient>(host_);
                cli->set_connection_timeout(30);
                cli->set_read_timeout(30);
                cli->set_keep_alive(true);
            }
            res = cli->Get(query);
            if (!res) { tl_ssl.erase(host_); continue; }
        } else {
            auto& cli = tl_http[host_];
            if (!cli) {
                cli = std::make_unique<httplib::Client>(host_);
                cli->set_connection_timeout(30);
                cli->set_read_timeout(30);
                cli->set_keep_alive(true);
            }
            res = cli->Get(query);
            if (!res) { tl_http.erase(host_); continue; }
        }

        if (res->status != 200) continue;

        try {
            auto j = nlohmann::json::parse(res->body);
            if (!j.contains(cfg_.digits_json_field) || !j[cfg_.digits_json_field].is_string())
                throw std::runtime_error(
                    "ApiDigitSource: response missing field \"" + cfg_.digits_json_field + "\"");
            return j[cfg_.digits_json_field].get<std::string>();
        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error(
                std::string("ApiDigitSource: JSON parse error: ") + e.what());
        }
    }
    throw std::runtime_error(
        "ApiDigitSource: fetch_range failed after 3 retries at offset "
        + std::to_string(start));
}

std::size_t ApiDigitSource::read_at(std::size_t digit_offset, uint8_t* buffer, std::size_t n) {
    std::size_t filled = 0;
    while (filled < n) {
        int batch = static_cast<int>(
            std::min(static_cast<std::size_t>(cfg_.max_per_request), n - filled));
        std::string digits = fetch_range(digit_offset + filled, batch);
        for (char c : digits) {
            if (filled >= n) break;
            buffer[filled++] = static_cast<uint8_t>(c - '0');
        }
    }
    return filled;
}

std::size_t ApiDigitSource::next_chunk(uint8_t* buffer, std::size_t n) {
    std::lock_guard<std::mutex> lock(seq_mutex_);
    auto got = read_at(seq_pos_, buffer, n);
    seq_pos_ += got;
    return got;
}

void ApiDigitSource::reset() {
    std::lock_guard<std::mutex> lock(seq_mutex_);
    seq_pos_ = 0;
}

bool ApiDigitSource::is_finite() const {
    return max_digits_ > 0;
}

std::optional<uint64_t> ApiDigitSource::estimated_length() const {
    if (max_digits_ == 0) return std::nullopt;
    return max_digits_;
}
