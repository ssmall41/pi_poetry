#pragma once
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

// Lock-free SPSC queue (single producer, single consumer).
// Capacity is rounded up to the next power of 2; back-pressure on push() is
// spin-yield (no futex). Shutdown is signaled via set_done(); is_exhausted()
// returns true once done and empty.
template<typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity) {
        std::size_t cap = 1;
        while (cap < capacity) cap <<= 1;
        buf_.resize(cap);
        mask_ = cap - 1;
    }

    // Move constructor: safe to call only before concurrent use (during setup).
    SpscQueue(SpscQueue&& o) noexcept
        : head_(o.head_.load(std::memory_order_relaxed))
        , tail_(o.tail_.load(std::memory_order_relaxed))
        , buf_(std::move(o.buf_))
        , mask_(o.mask_)
        , done_(o.done_.load(std::memory_order_relaxed))
    {}

    void push(T item) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        while (t - head_.load(std::memory_order_acquire) > mask_)
            std::this_thread::yield();
        buf_[t & mask_] = std::move(item);
        tail_.store(t + 1, std::memory_order_release);
    }

    // Returns false if empty. Exactly one consumer at a time (SPSC contract).
    bool pop(T& out) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire))
            return false;
        out = std::move(buf_[h & mask_]);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    void set_done() {
        done_.store(true, std::memory_order_release);
    }

    bool is_exhausted() const {
        if (!done_.load(std::memory_order_acquire))
            return false;
        // Acquire on done_ establishes happens-before for all producer writes,
        // so tail_ is already visible; head_ is relaxed (consumer's own write).
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::vector<T> buf_;
    std::size_t mask_{0};
    std::atomic<bool> done_{false};
};
