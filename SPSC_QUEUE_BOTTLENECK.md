# Per-Finder SPSC Queue: Eliminating the Combo-Stage Bottleneck

This branch implements these features over the main branch.

## Problem

The parallel pipeline had a shared `BoundedQueue<ComboPackage>` between the
word-finder stage (stage 3) and the phrase-scanner stage (stage 4). All N
finder threads pushed into one queue, and all M scanner threads popped from
it. That single queue was a contention bottleneck under high combo volume —
each push and pop required mutual exclusion, serializing what should be
parallel work.

## Solution

Replace the single shared `BoundedQueue<ComboPackage>` with **N independent
SPSC queues**, one owned by each finder thread. Each scanner thread is
assigned a subset of queues and polls them in round-robin.

```
Before:
  finder-0 ──┐
  finder-1 ──┤──► combo_q (BoundedQueue, mutex-protected) ──► scanner-0..M
  finder-2 ──┘

After:
  finder-0 ──► combo_qs[0] ──┐
  finder-1 ──► combo_qs[1] ──┤──► scanner-j (owns queues where id % M == j)
  finder-2 ──► combo_qs[2] ──┘
```

Because each queue has exactly one producer and one consumer (SPSC contract),
no locking is needed at all.

## New Component: `SpscQueue<T>`

`include/pipeline/SpscQueue.hpp` is a header-only, lock-free SPSC ring buffer.

Key design decisions:

| Decision | Detail |
|---|---|
| Power-of-2 capacity | Rounds up on construction; uses bitmask instead of modulo |
| Cache-line isolation | `head_` and `tail_` are `alignas(64)` to prevent false sharing |
| Back-pressure | `push()` spin-yields when full (no futex; appropriate for high-throughput short waits) |
| Shutdown protocol | Producer calls `set_done()`; consumer calls `is_exhausted()` (done AND empty) |
| Move constructor | Safe before concurrent use; used during pipeline vector construction |

### Memory ordering

- `push()`: relaxed load of `tail_`, acquire load of `head_` (see the slot is
  free), write slot, release store of `tail_` (publishes item).
- `pop()`: relaxed load of `head_`, acquire load of `tail_` (sees published
  item), read slot, release store of `head_` (frees slot).
- `is_exhausted()`: acquire load of `done_` establishes happens-before for all
  producer writes, so the subsequent relaxed loads of `head_`/`tail_` are safe.

## Pipeline Changes (`src/pipeline/Pipeline.cpp`)

### Queue allocation

```cpp
const int N = cfg.finder_threads;
const int M = cfg.scanner_threads;
const int Q = std::max(N, M);
const std::size_t per_q_cap = std::max<std::size_t>(1, cfg.combo_q_capacity / N);
std::vector<SpscQueue<ComboPackage>> combo_qs(Q, per_q_cap);
// Extra queues (Q > N) are immediately marked done — scanner threads assigned
// to them see is_exhausted() == true and exit cleanly.
for (int i = N; i < Q; ++i) combo_qs[i].set_done();
```

The total capacity across all queues equals `combo_q_capacity` (the same
config knob as before), so memory usage is unchanged.

### Finder threads (stage 3)

Each finder thread pushes exclusively to `combo_qs[i]` via the existing
`WordFinderWorker::process()` callback interface. On exit it calls
`combo_qs[i].set_done()` to signal its consumer(s).

### Scanner threads (stage 4)

Thread `j` owns queues `{j, j+M, j+2M, ...}` and polls them round-robin.
When all owned queues are exhausted it decrements an `atomic<int>
active_scanners`; the last scanner to exit calls `phrase_q.set_done()` to
signal the writer stage.

### Thread lifecycle

`StageRunner` is no longer used for stages 3 and 4 — they are plain
`std::thread` vectors, joined directly. Stages 1, 2, and 5 are unchanged.

## Handling Asymmetric Thread Counts

| N (finders) vs M (scanners) | Behaviour |
|---|---|
| N == M | 1:1, each scanner owns exactly one queue |
| N > M | Some scanners own ⌈N/M⌉ queues, poll them round-robin |
| M > N | Extra scanners get immediately-exhausted queues and exit immediately |

## Tests

### Unit tests (`tests/test_spsc_queue.cpp`)

Eight focused tests:

1. **PushPopFifoOrder** — items come out in insertion order
2. **PopReturnsFalseWhenEmpty** — pop() is non-blocking when empty
3. **PopReturnsFalseAfterDraining** — empty after last item consumed
4. **SetDoneAllowsDrainBeforeExhaustion** — items pushed before set_done() are
   still consumable
5. **IsExhaustedFalseBeforeSetDone** — exhaustion requires done signal
6. **IsExhaustedTrueOnEmptyAfterSetDone** — done + empty = exhausted
7. **BackPressureBlocksUntilConsumerPops** — push() blocks a producer thread
   until a slot is freed
8. **SpscConcurrentAllItemsReceived** — 10 000-item concurrent producer/consumer
   sum check
9. **MultipleIndependentQueuesRunConcurrently** — 5 parallel SPSC pairs,
   models the per-finder design

### Integration tests (`tests/test_parallel_pipeline.cpp`, cycles 7–9)

All three compare parallel output to serial output on the same input:

| Test | Config | What it validates |
|---|---|---|
| `SpscQueues_FinderEqualsScanner` | N=3, M=3 | 1:1 queue assignment |
| `SpscQueues_MoreScannersThanFinders` | N=2, M=4 | extra scanner threads exit cleanly |
| `SpscQueues_MoreFindersThanScanners` | N=5, M=2 | scanners multiplex multiple queues |
