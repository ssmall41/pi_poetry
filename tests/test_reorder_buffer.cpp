#include <gtest/gtest.h>
#include "pipeline/ReorderBuffer.hpp"
#include <string>
#include <vector>

// ── Single item per chunk (ETL-style: one package per chunk) ─────────────────

TEST(ReorderBuffer, SingleItemPerChunk_InOrder) {
    ReorderBuffer<std::string> rb;
    rb.submit(0, 0, true, "A");
    rb.submit(1, 0, true, "B");

    std::vector<std::string> out;
    rb.drain([&](std::string& s) { out.push_back(s); });
    EXPECT_EQ(out, (std::vector<std::string>{"A", "B"}));
}

TEST(ReorderBuffer, SingleItemPerChunk_OutOfOrder) {
    ReorderBuffer<std::string> rb;
    rb.submit(1, 0, true, "B");

    std::vector<std::string> out;
    rb.drain([&](std::string& s) { out.push_back(s); });
    EXPECT_TRUE(out.empty());  // chunk 0 not yet present

    rb.submit(0, 0, true, "A");
    rb.drain([&](std::string& s) { out.push_back(s); });
    EXPECT_EQ(out, (std::vector<std::string>{"A", "B"}));
}

// ── Multiple items per chunk (AllCombos-style: N packages per chunk) ─────────

TEST(ReorderBuffer, MultipleItemsPerChunk_InOrder) {
    ReorderBuffer<std::string> rb;
    rb.submit(0, 0, false, "A0");
    rb.submit(0, 1, true,  "A1");

    std::vector<std::string> out;
    rb.drain([&](std::string& s) { out.push_back(s); });
    EXPECT_EQ(out, (std::vector<std::string>{"A0", "A1"}));
}

TEST(ReorderBuffer, MultipleItemsPerChunk_IntraOutOfOrder) {
    ReorderBuffer<std::string> rb;
    rb.submit(0, 1, true,  "A1");  // final arrives first
    rb.submit(0, 0, false, "A0");

    std::vector<std::string> out;
    rb.drain([&](std::string& s) { out.push_back(s); });
    EXPECT_EQ(out, (std::vector<std::string>{"A0", "A1"}));
}

// ── Partial chunk blocks drain ───────────────────────────────────────────────

TEST(ReorderBuffer, PartialChunkBlocksDrain) {
    ReorderBuffer<std::string> rb;
    rb.submit(0, 0, false, "A0");  // is_final=false, no final yet

    std::vector<std::string> out;
    rb.drain([&](std::string& s) { out.push_back(s); });
    EXPECT_TRUE(out.empty());

    rb.submit(0, 1, true, "A1");
    rb.drain([&](std::string& s) { out.push_back(s); });
    EXPECT_EQ(out, (std::vector<std::string>{"A0", "A1"}));
}

// ── drain_all flushes everything complete ─────────────────────────────────────

TEST(ReorderBuffer, DrainAll) {
    ReorderBuffer<int> rb;
    rb.submit(0, 0, true, 10);
    rb.submit(1, 0, true, 20);
    rb.submit(2, 0, true, 30);

    std::vector<int> out;
    rb.drain_all([&](int& v) { out.push_back(v); });
    EXPECT_EQ(out, (std::vector<int>{10, 20, 30}));
}

// ── Mixed: multi-item chunk followed by single-item chunk ────────────────────

TEST(ReorderBuffer, MixedChunkSizes) {
    ReorderBuffer<int> rb;
    rb.submit(0, 0, false, 1);
    rb.submit(0, 1, false, 2);
    rb.submit(0, 2, true,  3);
    rb.submit(1, 0, true,  4);

    std::vector<int> out;
    rb.drain([&](int& v) { out.push_back(v); });
    EXPECT_EQ(out, (std::vector<int>{1, 2, 3, 4}));
}
