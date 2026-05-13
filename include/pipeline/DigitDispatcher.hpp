#pragma once
#include "digit_source/DigitSource.hpp"
#include "pipeline/WorkPackage.hpp"
#include <atomic>
#include <optional>
#include <vector>

// Assigns monotonic seq_ids to fixed-size chunks and reads each chunk
// (real data + lookahead) via DigitSource::read_at. Lock-free: multiple
// threads may call next() concurrently.
class DigitDispatcher {
public:
    DigitDispatcher(DigitSource& source, std::size_t chunk_size, std::size_t lookahead_digits)
        : source_(source), chunk_size_(chunk_size), lookahead_digits_(lookahead_digits) {}

    std::optional<DigitPackage> next() {
        std::size_t seq_id = next_seq_.fetch_add(1, std::memory_order_relaxed);
        std::size_t offset = seq_id * chunk_size_;
        std::size_t read_size = chunk_size_ + lookahead_digits_;
        std::vector<uint8_t> buf(read_size);
        std::size_t n = source_.read_at(offset, buf.data(), read_size);
        if (n == 0) return std::nullopt;
        buf.resize(n);
        DigitPackage pkg;
        pkg.seq_id              = seq_id;
        pkg.global_digit_offset = offset;
        pkg.num_real_digits     = std::min(chunk_size_, n);
        pkg.digits              = std::move(buf);
        return pkg;
    }

private:
    DigitSource& source_;
    std::size_t chunk_size_;
    std::size_t lookahead_digits_;
    std::atomic<std::size_t> next_seq_{0};
};
