#pragma once
#include <cstddef>
#include <functional>
#include <map>

// Two-level reorder buffer keyed by (chunk_id, intra_chunk_seq_id).
// A chunk is ready to drain when its final package has arrived
// (is_final=true marks the last intra index K) and all 0..K packages
// for that chunk are present.  Chunks drain in chunk_id order.
template<typename T>
class ReorderBuffer {
public:
    void submit(std::size_t chunk_id, std::size_t intra_id, bool is_final, T item) {
        pending_[chunk_id][intra_id] = std::move(item);
        if (is_final)
            final_idx_[chunk_id] = intra_id;
    }

    void drain(const std::function<void(T&)>& cb) {
        while (true) {
            auto fit = final_idx_.find(next_chunk_);
            if (fit == final_idx_.end()) break;          // no final yet for this chunk

            std::size_t K   = fit->second;
            auto& inner     = pending_[next_chunk_];
            if (inner.size() != K + 1) break;            // not all intra packages arrived yet

            for (std::size_t i = 0; i <= K; ++i)
                cb(inner.at(i));

            pending_.erase(next_chunk_);
            final_idx_.erase(next_chunk_);
            ++next_chunk_;
        }
    }

    void drain_all(const std::function<void(T&)>& cb) {
        drain(cb);
    }

private:
    std::map<std::size_t, std::map<std::size_t, T>> pending_;
    std::map<std::size_t, std::size_t>               final_idx_;
    std::size_t next_chunk_{0};
};
