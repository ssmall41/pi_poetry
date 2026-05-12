#pragma once
#include <cstddef>
#include <functional>
#include <map>

// Collects items indexed by a monotonically-assigned seq_id and emits them
// in strict ascending order via a callback.
// submit() + drain() can be called concurrently on different items, but the
// caller is responsible for external synchronization if multiple threads call
// these methods on the same instance.
template<typename T>
class ReorderBuffer {
public:
    void submit(std::size_t seq_id, T item) {
        pending_[seq_id] = std::move(item);
    }

    // Flush all consecutively-ready items starting from next_emit_.
    void drain(const std::function<void(T&)>& cb) {
        while (pending_.count(next_emit_)) {
            cb(pending_.at(next_emit_));
            pending_.erase(next_emit_);
            ++next_emit_;
        }
    }

    // Flush everything remaining (call once after all items submitted).
    void drain_all(const std::function<void(T&)>& cb) {
        drain(cb);
    }

private:
    std::map<std::size_t, T> pending_;
    std::size_t next_emit_{0};
};
