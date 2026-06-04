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
    DigitDispatcher(DigitSource& source, std::size_t chunk_size,
                    std::size_t lookahead_digits, uint64_t max_digits = 0)
        : source_(source), chunk_size_(chunk_size), lookahead_digits_(lookahead_digits),
          max_digits_(max_digits) {}

    std::optional<DigitPackage> next() {
        std::size_t seq_id = next_seq_.fetch_add(1, std::memory_order_relaxed);
        std::size_t offset = seq_id * chunk_size_;
        if (max_digits_ > 0 && offset >= max_digits_) return std::nullopt;

        std::size_t effective_chunk = chunk_size_;
        if (max_digits_ > 0 && offset + chunk_size_ > max_digits_)
            effective_chunk = max_digits_ - offset;

        std::size_t read_size = effective_chunk + lookahead_digits_;
        std::vector<uint8_t> buf(read_size);
        std::size_t n = source_.read_at(offset, buf.data(), read_size);
        if (n == 0) return std::nullopt;
        buf.resize(n);
        DigitPackage pkg;
        pkg.seq_id              = seq_id;
        pkg.global_digit_offset = offset;
        pkg.num_real_digits     = std::min(effective_chunk, n);
        pkg.digits              = std::move(buf);
        return pkg;
    }

private:
    DigitSource& source_;
    std::size_t chunk_size_;
    std::size_t lookahead_digits_;
    uint64_t    max_digits_{0};
    std::atomic<std::size_t> next_seq_{0};
};
