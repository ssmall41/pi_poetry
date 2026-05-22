#pragma once
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

// Thread-safe bounded FIFO with back-pressure.
// push() blocks when the queue is at capacity.
// pop() blocks until an item is available or the queue is done.
// set_done() signals no more items will be pushed; pop() returns false
// once done and empty.
template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    void push(T item) {
        std::unique_lock lock(mu_);
        not_full_.wait(lock, [&] { return q_.size() < capacity_ || done_; });
        if (done_) return;
        q_.push(std::move(item));
        not_empty_.notify_one();
    }

    // Returns false when done+empty.
    bool pop(T& out) {
        std::unique_lock lock(mu_);
        not_empty_.wait(lock, [&] { return !q_.empty() || done_; });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        not_full_.notify_one();
        return true;
    }

    void set_done() {
        std::unique_lock lock(mu_);
        done_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const {
        std::unique_lock lock(mu_);
        return q_.size();
    }

private:
    mutable std::mutex mu_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::queue<T> q_;
    std::size_t capacity_;
    bool done_{false};
};
