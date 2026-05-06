#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

class DigitSource {
public:
    virtual ~DigitSource() = default;

    // Fills buffer[0..n-1] with the next n digit values (0-9).
    // Returns the actual count written (< n only at end-of-stream).
    virtual std::size_t next_chunk(uint8_t* buffer, std::size_t n) = 0;

    // Resets the source to the beginning of the sequence.
    virtual void reset() = 0;

    virtual bool is_finite() const = 0;

    // Returns total digit count if known.
    virtual std::optional<uint64_t> estimated_length() const = 0;

    // Returns the numeric base of digits produced (10 for decimal).
    virtual int base() const = 0;
};
