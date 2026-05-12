#pragma once
#include "digit_source/DigitSource.hpp"
#include "pipeline/WorkPackage.hpp"
#include <mutex>
#include <optional>

// Wraps a DigitSource and atomically assigns monotonic seq_ids to chunks.
// Multiple threads may call next() concurrently; the mutex ensures each chunk
// gets a unique seq_id that matches its position in the sequence.
class DigitDispatcher {
public:
    explicit DigitDispatcher(DigitSource& source) : source_(source) {}

    std::optional<DigitPackage> next(std::size_t chunk_size) {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<uint8_t> buf(chunk_size);
        std::size_t n = source_.next_chunk(buf.data(), chunk_size);
        if (n == 0) return std::nullopt;
        buf.resize(n);
        DigitPackage pkg;
        pkg.seq_id = next_seq_++;
        pkg.global_digit_offset = next_digit_offset_;
        pkg.digits = std::move(buf);
        next_digit_offset_ += n;
        return pkg;
    }

private:
    DigitSource& source_;
    std::mutex mu_;
    std::size_t next_seq_{0};
    std::size_t next_digit_offset_{0};
};
