#include <gtest/gtest.h>
#include "pipeline/BoundedQueue.hpp"
#include <atomic>
#include <thread>
#include <vector>

// ── Basic single-thread push/pop ─────────────────────────────────────────────

TEST(BoundedQueue, PushPopBasic) {
    BoundedQueue<int> q(4);
    q.push(1);
    q.push(2);
    int v = 0;
    EXPECT_TRUE(q.pop(v)); EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.pop(v)); EXPECT_EQ(v, 2);
}

// ── set_done() causes pop() to return false when empty ───────────────────────

TEST(BoundedQueue, ReturnsFalseWhenDoneAndEmpty) {
    BoundedQueue<int> q(4);
    q.push(42);
    q.set_done();
    int v = 0;
    EXPECT_TRUE(q.pop(v));   // still one item
    EXPECT_EQ(v, 42);
    EXPECT_FALSE(q.pop(v));  // now done+empty
}

TEST(BoundedQueue, SetDoneOnEmptyQueueReturnsFalseImmediately) {
    BoundedQueue<int> q(4);
    q.set_done();
    int v = 0;
    EXPECT_FALSE(q.pop(v));
}

// ── Bounded capacity: push blocks until consumer pops ────────────────────────

TEST(BoundedQueue, BlocksWhenFull) {
    BoundedQueue<int> q(2);
    q.push(1);
    q.push(2);
    // Queue is now full. A push in a background thread should block until we pop.
    std::atomic<bool> push_returned{false};
    std::thread producer([&] {
        q.push(3);
        push_returned.store(true);
    });
    // Give the producer a moment to block
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(push_returned.load());
    int v = 0;
    q.pop(v);  // consume one → unblocks producer
    producer.join();
    EXPECT_TRUE(push_returned.load());
}

// ── size() reflects current count ───────────────────────────────────────────

TEST(BoundedQueue, SizeReflectsCount) {
    BoundedQueue<int> q(8);
    EXPECT_EQ(q.size(), 0u);
    q.push(1);
    q.push(2);
    EXPECT_EQ(q.size(), 2u);
    int v = 0;
    q.pop(v);
    EXPECT_EQ(q.size(), 1u);
}

// ── Concurrent producers and consumers ──────────────────────────────────────

TEST(BoundedQueue, ConcurrentProducersConsumers) {
    const int N = 1000;
    BoundedQueue<int> q(8);
    std::atomic<int> sum_produced{0};
    std::atomic<int> sum_consumed{0};

    // 4 producers, each push N/4 items
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t) {
        producers.emplace_back([&, t] {
            for (int i = t * (N / 4); i < (t + 1) * (N / 4); ++i) {
                q.push(i);
                sum_produced.fetch_add(i);
            }
        });
    }

    // 4 consumers
    std::vector<std::thread> consumers;
    for (int t = 0; t < 4; ++t) {
        consumers.emplace_back([&] {
            int v = 0;
            while (q.pop(v))
                sum_consumed.fetch_add(v);
        });
    }

    for (auto& p : producers) p.join();
    q.set_done();
    for (auto& c : consumers) c.join();

    EXPECT_EQ(sum_produced.load(), sum_consumed.load());
}
