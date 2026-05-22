#pragma once
#include "pipeline/BoundedQueue.hpp"
#include "pipeline/StageWorker.hpp"
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Runs N workers over an input queue → output queue pair.
// Each worker thread pops a package, logs the claim, processes it, and pushes
// the result. The last worker to finish calls out_q.set_done().
template<typename In, typename Out>
class StageRunner {
public:
    StageRunner(std::vector<std::unique_ptr<StageWorker<In, Out>>> workers,
                BoundedQueue<In>& in_q,
                BoundedQueue<Out>& out_q,
                bool debug = true)
        : workers_(std::move(workers)), in_q_(in_q), out_q_(out_q),
          active_(static_cast<int>(workers_.size())), debug_(debug) {}

    void start() {
        for (int i = 0; i < static_cast<int>(workers_.size()); ++i) {
            threads_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    void join() {
        for (auto& t : threads_) t.join();
    }

private:
    void worker_loop(int worker_id) {
        In pkg;
        while (in_q_.pop(pkg)) {
            if (debug_) {
                auto in_remaining = in_q_.size();
                auto out_pending  = out_q_.size();
                std::lock_guard<std::mutex> lock(cout_mu_);
                std::cout << "[" << workers_[worker_id]->stage_name()
                          << "] worker " << worker_id
                          << " claimed package " << pkg_seq_id(pkg)
                          << " (in: " << in_remaining << " remaining"
                          << ", out: " << out_pending << " pending)\n";
            }
            workers_[worker_id]->process(std::move(pkg),
                [&](Out result) { out_q_.push(std::move(result)); });
        }
        if (active_.fetch_sub(1) == 1)
            out_q_.set_done();
    }

    template<typename P>
    static std::size_t pkg_seq_id(const P& p) {
        if constexpr (requires { p.chunk_id; })
            return p.chunk_id;
        else
            return p.seq_id;
    }

    std::vector<std::unique_ptr<StageWorker<In, Out>>> workers_;
    BoundedQueue<In>& in_q_;
    BoundedQueue<Out>& out_q_;
    std::atomic<int> active_;
    bool debug_;
    std::vector<std::thread> threads_;
    std::mutex cout_mu_;
};
