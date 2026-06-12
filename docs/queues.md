# Pi Poetry — Queue Architecture

The pipeline stages communicate exclusively through bounded queues. No stage holds a direct reference to another; all data flows forward through a queue, and all back-pressure flows backward through the same queue.

---

## Queue Placement

```
  Digit Feeders
        │
    digit_q
        │
        ▼
  Digit Mapper
        │
    letter_q
        │
        ▼
  Word Finder
        │
    combo_q
        │
        ▼
 Phrase Scanner
        │
    phrase_q
        │
        ▼
  Writer Thread
```

Each arrow represents one `BoundedQueue<T>` instance. The four queues and their element types are:

| Queue | Element type | Filled by | Drained by |
|---|---|---|---|
| `digit_q` | `DigitPackage` | Digit Feeder threads | Digit Mapper workers |
| `letter_q` | `LetterPackage` | Digit Mapper workers | Word Finder workers |
| `combo_q` | `ComboPackage` | Word Finder workers | Phrase Scanner workers |
| `phrase_q` | `PhrasePackage` | Phrase Scanner workers | Writer thread |

---

## `BoundedQueue<T>`

[include/pipeline/BoundedQueue.hpp](../include/pipeline/BoundedQueue.hpp) is a thread-safe FIFO with a fixed maximum capacity — the "bounded" in the name refers to this capacity limit, which is what gives the queue its back-pressure behaviour. Its interface has three meaningful operations:

```cpp
void push(T item);   // blocks if full
bool pop(T& out);    // blocks if empty; returns false when done+empty
void set_done();     // signals no more pushes will ever arrive
```

Internally it holds a `std::mutex`, two `std::condition_variable`s (`not_full_` and `not_empty_`), and a `std::queue<T>`.

**`push`** acquires the lock and waits on `not_full_` until `q_.size() < capacity_ || done_`. If `done_` is already set it returns immediately without inserting anything, so late pushes after shutdown are silently dropped. Otherwise it moves the item in and notifies `not_empty_`.

**`pop`** acquires the lock and waits on `not_empty_` until `!q_.empty() || done_`. When it wakes it checks whether the queue is actually empty (it might have been woken by `set_done()` on an empty queue) and returns `false` in that case. When it does remove an item it notifies `not_full_`, releasing any blocked producer.

**`set_done`** sets the `done_` flag under the lock and then calls `notify_all` on *both* condition variables. Broadcasting to both is important: producers blocked waiting for space need to wake up and abort their push, and consumers blocked waiting for data need to wake up and drain the remaining items before returning `false`.

There are no timeouts anywhere. Every wait is indefinite — this is safe because `set_done()` is always called eventually, and the sequence of calls is guaranteed by the atomic counters described below.

---

## How Multiple Threads Use Each Queue

### Digit feeders → `digit_q`

The Digit Feeder stage uses a pool of `digit_threads` threads. Each thread calls `dispatcher.next()` in a loop and pushes the returned `DigitPackage` directly onto `digit_q`. The threads do not coordinate with each other on the push side; `BoundedQueue::push` provides all necessary serialization.

When a feeder thread's loop ends (the dispatcher has no more chunks), it atomically decrements a shared counter:

```cpp
std::atomic<int> active_feeders{cfg.digit_threads};
// inside each feeder thread:
if (active_feeders.fetch_sub(1) == 1)
    digit_q.set_done();
```

`fetch_sub` returns the value *before* the decrement, so the thread that observes `1` is the last one still running. That thread — and only that thread — calls `digit_q.set_done()`, telling all downstream consumers that no more digit packages will ever arrive.

### Middle stages (Digit Mapper, Word Finder, Phrase Scanner) → their output queues

These three stages all run through `StageRunner<In, Out>` ([include/pipeline/StageRunner.hpp](../include/pipeline/StageRunner.hpp)), which applies the same pattern generically. `StageRunner` holds:

- a vector of `StageWorker` instances, one per thread,
- a reference to the upstream `BoundedQueue<In>`,
- a reference to the downstream `BoundedQueue<Out>`,
- `std::atomic<int> active_` initialized to the worker count.

Each worker thread runs this loop:

```cpp
In pkg;
while (in_q_.pop(pkg)) {
    workers_[worker_id]->process(std::move(pkg),
        [&](Out result) { out_q_.push(std::move(result)); });
}
if (active_.fetch_sub(1) == 1)
    out_q_.set_done();
```

`in_q_.pop` blocks until either a package is available or the upstream stage calls `set_done()` on that queue. When `set_done()` has been called *and* the queue is empty, `pop` returns `false` and the loop exits. The same last-thread-wins atomic then propagates the shutdown signal one stage further downstream.

Because multiple workers share the same input queue, `pop` acts as a work-stealing mechanism: whichever thread is free picks up the next package. The `StageWorker::process` callback emits zero or more output packages to the downstream queue by calling the provided lambda, which in turn calls `out_q_.push`. Multiple workers may be calling `out_q_.push` concurrently, but `BoundedQueue::push` is fully thread-safe.

### Writer thread → terminal consumer

The writer thread is a single thread that reads from `phrase_q`:

```cpp
PhrasePackage pp;
while (phrase_q.pop(pp)) {
    reorder.submit(pp.chunk_id, pp.intra_chunk_seq_id,
                   pp.final_package_in_chunk, std::move(pp));
    reorder.drain(flush_phrase);
}
reorder.drain_all(flush_phrase);
```

It does not push to any downstream queue; it writes results to disk. When `phrase_q.pop` returns `false` the writer flushes any remaining buffered output and the thread exits.

---

## Shutdown Signal Propagation

Shutdown cascades automatically stage-by-stage:

1. `DigitDispatcher` runs out of chunks → all feeder threads exit → last feeder calls `digit_q.set_done()`.
2. Digit Mapper workers drain `digit_q` → all mapper workers exit → last mapper calls `letter_q.set_done()`.
3. Word Finder workers drain `letter_q` → all finder workers exit → last finder calls `combo_q.set_done()`.
4. Phrase Scanner workers drain `combo_q` → all scanner workers exit → last scanner calls `phrase_q.set_done()`.
5. Writer thread drains `phrase_q` → `pop` returns `false` → writer exits.

No global shutdown flag exists. Each stage learns about shutdown purely through the return value of `pop` on its own input queue, which is only set after the upstream stage has finished and the queue is empty. This means every package that was ever pushed is guaranteed to be processed before the stage exits.

---

## Queue Sizes

Queue capacity is read from the TOML configuration file. Each stage section has a `queue_size` key; the defaults are all 16:

```toml
[digit_source]
queue_size = 16   # capacity of digit_q

[digit_mapper]
queue_size = 16   # capacity of letter_q

[word_finder]
queue_size = 16   # capacity of combo_q

[phrase_scanner]
queue_size = 16   # capacity of phrase_q
```

The values are loaded in [src/main.cpp](../src/main.cpp) and passed through `Pipeline::ParallelConfig`:

```cpp
std::size_t digit_q_capacity  = config["digit_source"]["queue_size"].value_or(16);
std::size_t letter_q_capacity = config["digit_mapper"]["queue_size"].value_or(16);
std::size_t combo_q_capacity  = config["word_finder"]["queue_size"].value_or(16);
std::size_t phrase_q_capacity = config["phrase_scanner"]["queue_size"].value_or(16);
```

**Choosing a size.** A small queue provides tight back-pressure: a fast upstream stage is throttled almost immediately when the downstream stage falls behind, which keeps memory usage low. A large queue decouples the two stages so they can run at different speeds without blocking each other. This matters most when one stage is much more expensive than its neighbours — for example, the word finder is CPU-bound and is usually run with many threads, so giving its output queue (`combo_q`) plenty of capacity lets the finders keep producing instead of stalling whenever the phrase scanner momentarily falls behind.

A queue that is too small causes excessive context switching as threads block and unblock. A queue that is too large wastes memory and may hide performance imbalances between stages. Profiling with the `pipeline.debug = true` flag (which logs each package claim along with the current in-queue and out-queue depths) is the recommended way to tune these values.

---

## Important Implementation Details

**Back-pressure is automatic.** Because `push` blocks when the queue is full, a fast producer naturally slows down to match a slow consumer. No explicit rate limiting or sleep calls are needed anywhere in the pipeline.

**`set_done` unblocks both sides.** After `set_done()` is called, both blocked producers (waiting for space) and blocked consumers (waiting for data) are woken. Without the `notify_all` on `not_full_`, a producer blocked waiting for space in a full queue whose consumer has exited would deadlock forever.

**Late pushes are silently dropped.** If a producer calls `push` after `set_done()` has been called, the item is discarded. In normal operation this never happens because producers exit their loops before the downstream stage ever calls `set_done()`. The guard exists as a safety net against races that could otherwise corrupt the queue state.

**The atomic last-thread-wins pattern is the only coordination between workers within a stage.** Workers do not signal each other directly; they only contend on the shared `BoundedQueue` mutexes. The `active_` atomic in `StageRunner` (and the equivalent `active_feeders` for the feeder stage) is the sole mechanism for determining which worker is responsible for calling `set_done()` on the output queue.

**All threads are joined before the pipeline returns.** `Pipeline::run_parallel` joins the feeder threads, then the three `StageRunner` objects (via their own `join()` methods), then the writer thread. Because each stage only exits after fully draining its input queue, this join order guarantees that all output has been written to disk before the function returns.
