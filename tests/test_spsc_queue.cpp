#include <gtest/gtest.h>
#include "pipeline/SpscQueue.hpp"
#include <atomic>
#include <thread>
#include <vector>

// ── Cycle 1: basic push/pop preserves FIFO order ─────────────────────────────

TEST(SpscQueue, PushPopFifoOrder) {
    SpscQueue<int> q(4);
    q.push(1);
    q.push(2);
    q.push(3);
    int v = 0;
    EXPECT_TRUE(q.pop(v)); EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.pop(v)); EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.pop(v)); EXPECT_EQ(v, 3);
}

// ── Cycle 2: pop() returns false when empty ───────────────────────────────────

TEST(SpscQueue, PopReturnsFalseWhenEmpty) {
    SpscQueue<int> q(4);
    int v = 0;
    EXPECT_FALSE(q.pop(v));
}

TEST(SpscQueue, PopReturnsFalseAfterDraining) {
    SpscQueue<int> q(4);
    q.push(7);
    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_FALSE(q.pop(v));
}

// ── Cycle 3: set_done / is_exhausted ─────────────────────────────────────────

TEST(SpscQueue, SetDoneAllowsDrainBeforeExhaustion) {
    SpscQueue<int> q(4);
    q.push(42);
    q.set_done();
    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 42);
    EXPECT_FALSE(q.pop(v));
    EXPECT_TRUE(q.is_exhausted());
}

TEST(SpscQueue, IsExhaustedFalseBeforeSetDone) {
    SpscQueue<int> q(4);
    EXPECT_FALSE(q.is_exhausted());
}

TEST(SpscQueue, IsExhaustedTrueOnEmptyAfterSetDone) {
    SpscQueue<int> q(4);
    q.set_done();
    EXPECT_TRUE(q.is_exhausted());
}

// ── Cycle 4: back-pressure — push() spins when full ──────────────────────────

TEST(SpscQueue, BackPressureBlocksUntilConsumerPops) {
    SpscQueue<int> q(2);
    q.push(1);
    q.push(2);
    std::atomic<bool> push_returned{false};
    std::thread producer([&] {
        q.push(3);
        push_returned.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(push_returned.load());
    int v = 0;
    q.pop(v);
    producer.join();
    EXPECT_TRUE(push_returned.load());
    EXPECT_EQ(v, 1);
}

// ── Cycle 5: SPSC concurrent — 1 producer + 1 consumer, all items received ───

TEST(SpscQueue, SpscConcurrentAllItemsReceived) {
    const int N = 10000;
    SpscQueue<int> q(64);
    std::atomic<int> sum_produced{0};
    std::atomic<int> sum_consumed{0};

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            q.push(i);
            sum_produced.fetch_add(i, std::memory_order_relaxed);
        }
        q.set_done();
    });

    std::thread consumer([&] {
        int v = 0;
        while (!q.is_exhausted()) {
            if (q.pop(v))
                sum_consumed.fetch_add(v, std::memory_order_relaxed);
        }
        while (q.pop(v))
            sum_consumed.fetch_add(v, std::memory_order_relaxed);
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(sum_produced.load(), sum_consumed.load());
    EXPECT_EQ(sum_consumed.load(), N * (N - 1) / 2);
}

// ── Cycle 6: N independent SPSC queues running concurrently ──────────────────
// Models the per-finder-thread queue design: each queue has exactly one
// producer and one consumer, all operating in parallel.

TEST(SpscQueue, MultipleIndependentQueuesRunConcurrently) {
    const int NUM_QUEUES = 5;
    const int N = 10000;
    std::vector<SpscQueue<int>> queues;
    queues.reserve(NUM_QUEUES);
    for (int i = 0; i < NUM_QUEUES; ++i)
        queues.emplace_back(64);

    std::atomic<long long> total_consumed{0};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    for (int q = 0; q < NUM_QUEUES; ++q) {
        producers.emplace_back([&queues, q, N] {
            for (int i = 0; i < N; ++i)
                queues[q].push(i);
            queues[q].set_done();
        });
        consumers.emplace_back([&queues, q, &total_consumed] {
            long long local = 0;
            int v;
            for (;;) {
                if (queues[q].pop(v))
                    local += v;
                else if (queues[q].is_exhausted())
                    break;
            }
            while (queues[q].pop(v)) local += v;
            total_consumed.fetch_add(local, std::memory_order_relaxed);
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    const long long per_queue = (long long)N * (N - 1) / 2;
    EXPECT_EQ(total_consumed.load(), per_queue * NUM_QUEUES);
}
