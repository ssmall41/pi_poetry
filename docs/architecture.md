# Pi Poetry — Pipeline Architecture

Pi Poetry searches the decimal expansion of π for sequences of English words encoded in consecutive digit pairs. A four-stage pipeline converts raw digits into human-readable phrases.

---

## Pipeline Diagram

```
  pi digits file
        │
        │ pread()
        ▼
┌─────────────────────┐
│   Digit Feeder      │  × digit_threads
│   (DigitDispatcher) │
└─────────────────────┘
        │  DigitPackage
        ▼  digit_q  (BoundedQueue, cap 16)
┌─────────────────────┐
│   Digit Mapper      │  × mapper_threads
│   Worker            │
└─────────────────────┘
        │  LetterPackage
        ▼  letter_q  (BoundedQueue, cap 16)
┌─────────────────────┐
│   Word Finder       │  × finder_threads
│   Worker            │
└─────────────────────┘
        │  ComboPackage
        ▼  combo_q  (BoundedQueue, cap 16)
┌─────────────────────┐
│   Phrase Scanner    │  × scanner_threads
│   Worker            │
└─────────────────────┘
        │  PhrasePackage
        ▼  phrase_q  (BoundedQueue, cap 16)
┌─────────────────────┐
│   Writer            │  × 1
│   (ReorderBuffer)   │
└─────────────────────┘
        │
        ▼
  results.json
```

---

## Stage 1: Digit Source

**Purpose:** Read the raw digit file and emit fixed-size chunks of digit values to the rest of the pipeline.

**Key files:**
- [include/digit_source/DigitSource.hpp](../include/digit_source/DigitSource.hpp) — abstract interface
- [include/digit_source/FileDigitSource.hpp](../include/digit_source/FileDigitSource.hpp)
- [src/digit_source/FileDigitSource.cpp](../src/digit_source/FileDigitSource.cpp)

**Inputs:** A plain-text file containing one ASCII decimal digit per byte (e.g. `data/pi_100000000.txt`).

**Outputs:** Chunks of `uint8_t` values in the range 0–9, the number of which is configurable via `digit_source.chunk_size`. The chunk size is snapped up by the pipeline to a multiple of `digits_per_char` so downstream stages always receive complete digit sets.

**Key interface methods:**

| Method | Description |
|---|---|
| `next_chunk(buffer, n)` | Fill buffer with the next n digits; returns actual count written. Mutex-protected for thread safety. |
| `read_at(offset, buffer, n)` | Random-access read at a given digit offset. Used by the parallel pipeline's `DigitDispatcher`. |
| `reset()` | Rewind to the beginning of the digit sequence. |
| `is_finite()` | Returns `true`; file sources have a known end. |
| `estimated_length()` | Returns total digit count. |
| `base()` | Returns `10` (decimal). |

**Notes:** The file is opened with both an `ifstream` (for sequential reads) and a raw file descriptor (for `pread`-based random access). The parallel pipeline uses `read_at` exclusively so multiple feeder threads can read non-overlapping regions simultaneously.

---

## Stage 2: Digit Mapper

**Purpose:** Encode consecutive pairs of digits as lowercase letters, producing the character stream that the word finder will search.

**Key files:**
- [include/digit_mapper/DigitMapper.hpp](../include/digit_mapper/DigitMapper.hpp) — abstract interface
- [include/digit_mapper/TwoDigitBlockMapper.hpp](../include/digit_mapper/TwoDigitBlockMapper.hpp)
- [src/digit_mapper/TwoDigitBlockMapper.cpp](../src/digit_mapper/TwoDigitBlockMapper.cpp)

**Inputs:** A chunk of `uint8_t` digit values (0–9) from the digit source.

**Outputs:** A character buffer of lowercase letters (`a`–`z`), half as long as the input (2 digits → 1 character).

**Encoding algorithm:**

```
for each consecutive pair of digits (d₀, d₁):
    value     = d₀ × 10 + d₁     // 0–99
    character = 'a' + (value % 26) // a–z
```

Example: digits `3`, `1` → value `31` → `31 % 26 = 5` → `'f'`

**Key interface methods:**

| Method | Description |
|---|---|
| `map(digits, n_digits, out_chars, out_n)` | Convert n_digits into characters; sets out_n to the number of characters written. |
| `digits_per_char()` | Returns `2`. |
| `alphabet_size()` | Returns `26`. |
| `required_base()` | Returns `10`. |

**Notes:** The mapping is stateless and deterministic. Every 100-digit pair cycle maps to the full 26-letter alphabet with a slight non-uniform distribution (values 0–21 each appear once more than values 22–25 per 100 pairs). The pipeline optionally writes the full mapped character stream to `letter_sequence.txt` for debugging.

---

## Stage 3: Word Finder

**Purpose:** Scan the character stream for all occurrences of dictionary words using an Aho-Corasick automaton, then resolve overlapping matches according to a configured policy. Currently, consecutive word chains are emitted, but this might change in the future.

**Key files:**
- [include/word_finder/WordFinder.hpp](../include/word_finder/WordFinder.hpp) — abstract interface
- [include/word_finder/AhoCorasickCPU.hpp](../include/word_finder/AhoCorasickCPU.hpp)
- [src/word_finder/AhoCorasickCPU.cpp](../src/word_finder/AhoCorasickCPU.cpp)

**Inputs:** A character buffer of lowercase letters from the digit mapper, plus a global character offset indicating where in the full sequence this chunk begins.

**Outputs:** One or more ordered sequences of non-overlapping `WordMatch` values.

```cpp
struct WordMatch {
    string word;        // matched word
    size_t start;       // global character offset
    size_t length;      // character length of word
    bool consecutive;   // true if previous word ended exactly where this one starts
};
```

**Automaton construction (`build()`):**

1. Insert all dictionary words into a trie (nodes indexed by 0–25 for `a`–`z`).
2. BFS over the trie to set failure links (Knuth-Morris-Pratt style): when the automaton cannot extend a partial match, failure links redirect to the longest proper suffix that is a valid prefix.
3. Compute output links: each node's output link points to the nearest ancestor (via the failure chain) that is itself a complete word match.
4. Root's missing children loop back to root, so scanning never dereferences a null pointer.

**Scanning (`scan_chunk`):** For each input character the automaton follows the appropriate child edge, or falls back through failure links. At each position, all matches reachable via the output link chain are recorded with their global offset. The automaton state is preserved across calls so words that span chunk boundaries are detected correctly.

**Overlap policies:**

| Policy | Description |
|---|---|
| `EarliestThenLongest` | Sort raw matches by (start ascending, length descending) and greedily select non-overlapping matches. Produces one deterministic sequence per chunk. |
| `AllCombos` | Enumerate every possible non-overlapping chain via DFS, calling a callback for each chain. Used to maximise phrase variety. |

**Lookahead:** For chunk-boundary correctness, each chunk is extended by `max_word_length − 1` extra characters. Words whose start offset falls in the lookahead zone are discarded; only words whose start is in the real portion of the chunk are kept.

**Key interface methods:**

| Method | Description |
|---|---|
| `load_dictionary(path)` | Load word list (one word per line); respects `min_word_length`. |
| `build()` | Construct the automaton; must be called after loading the dictionary. |
| `scan_chunk(chunk, len, offset, state, raw_out)` | Stateful incremental scan; `state` carries the automaton node between calls. |
| `apply_etl_cb(raw, offset, on_chain)` | Apply `EarliestThenLongest`; calls `on_chain` 0–1 times (0 when no matches). |
| `apply_all_combos_cb(raw, offset, on_chain)` | Enumerate all chains; calls `on_chain` 0–N times. |
| `apply_policy_cb(raw, offset, on_chain)` | Dispatch to `apply_etl_cb` or `apply_all_combos_cb` based on the configured policy. |
| `set_min_word_length(n)` | Filter out short words before building the automaton. |

---

## Stage 4: Phrase Scanner

**Purpose:** Group directly consecutive word matches into phrases. Currently, phrases are emitted from the Word Finder stage. This stage does not actually find phrases, but rather, processes them for final output. This might change in the future.

**Key files:**
- [include/phrase_scanner/PhraseScanner.hpp](../include/phrase_scanner/PhraseScanner.hpp) — abstract interface
- [include/phrase_scanner/HumanReviewScanner.hpp](../include/phrase_scanner/HumanReviewScanner.hpp)
- [src/phrase_scanner/HumanReviewScanner.cpp](../src/phrase_scanner/HumanReviewScanner.cpp)

**Inputs:** An ordered sequence of `WordMatch` values from the word finder.

**Outputs:** A list of `PhraseMatch` values.

```cpp
struct PhraseMatch {
    size_t start_offset;    // global character offset of the first word
    vector<string> words;   // words in the phrase, in order
};
```

**Grouping algorithm:**

```
Sort words by start offset.
For each word:
    gap = word.start − previous_word.end
    if gap == 0:
        extend the current phrase
    else:
        emit current phrase, start a new one
```

Only words with zero gap (touching) are grouped into the same phrase.

**Streaming support:** In the **serial** pipeline, `process_words_streaming(batch, on_phrase)` and `flush_streaming(on_phrase)` are used to emit phrases one at a time without buffering the full result list. Each word chain is passed as a single batch followed immediately by a flush, so no accumulation across chunk boundaries occurs at the phrase layer. The parallel pipeline uses the non-streaming `process_words()` instead; because each chunk is scanned independently, phrases are bounded by chunk boundaries — two words that would form a phrase but fall in adjacent chunks will not be merged.

---

## Inter-Stage Queues (Parallel Mode)

All four queues are instances of `BoundedQueue<T>` ([include/pipeline/BoundedQueue.hpp](../include/pipeline/BoundedQueue.hpp)), a thread-safe FIFO backed by `std::queue` and a pair of condition variables. `push()` blocks when the queue is at capacity; `pop()` blocks until an item is available or the queue is marked done. This provides automatic back-pressure: if a downstream stage is slow, the upstream stage stalls rather than accumulating unbounded memory.

Every queue shares the same capacity, set by `queue_capacity` in `ParallelConfig` (default **16** packages).

---

#### `digit_q` — `BoundedQueue<DigitPackage>`

**Connects:** Digit feeder threads → Digit mapper workers

**Purpose:** Transfers raw digit chunks from the feeder threads (which read the source file) to the mapper workers (which encode digits to letters). Feeder threads push as soon as a chunk is read; mapper workers pop and immediately begin encoding.

**Capacity:** `queue_capacity` (default 16)

**Item type — `DigitPackage`:**

| Field | Type | Description |
|---|---|---|
| `seq_id` | `size_t` | Monotonically increasing chunk index; used downstream to restore output order. |
| `global_digit_offset` | `size_t` | Byte offset of the first digit of this chunk in the source file. |
| `num_real_digits` | `size_t` | Number of digits in the real (non-lookahead) portion of the chunk. |
| `digits` | `vector<uint8_t>` | Raw digit values (0–9). Includes the lookahead suffix (up to `max_word_length − 1` extra digit pairs) appended for boundary-spanning word detection. |

---

#### `letter_q` — `BoundedQueue<LetterPackage>`

**Connects:** Digit mapper workers → Word finder workers

**Purpose:** Carries encoded character chunks from the mapper to the word finder. Each package is the character-domain equivalent of a `DigitPackage`; the lookahead is preserved so the word finder can detect words that straddle chunk boundaries.

**Capacity:** `queue_capacity` (default 16)

**Item type — `LetterPackage`:**

| Field | Type | Description |
|---|---|---|
| `seq_id` | `size_t` | Same chunk index as the originating `DigitPackage`. |
| `global_char_offset` | `size_t` | Character-domain offset of the first real character (`global_digit_offset / 2`). |
| `num_real_chars` | `size_t` | Number of real (non-lookahead) characters. Words starting at or beyond `global_char_offset + num_real_chars` are discarded — they belong to the next chunk. |
| `chars` | `vector<char>` | Lowercase letters (`a`–`z`). Includes lookahead characters. |

---

#### `combo_q` — `BoundedQueue<ComboPackage>`

**Connects:** Word finder workers → Phrase scanner workers

**Purpose:** Carries a single resolved word chain (one result of the overlap policy) from the word finder to the phrase scanner. Under `EarliestThenLongest` each chunk produces exactly one `ComboPackage`; under `AllCombos` a chunk can produce many packages (one per enumerated chain), each with its own `intra_chunk_seq_id`.

**Capacity:** `queue_capacity` (default 16)

**Item type — `ComboPackage`:**

| Field | Type | Description |
|---|---|---|
| `chunk_id` | `size_t` | Chunk index; matches `seq_id` of the source `LetterPackage`. |
| `intra_chunk_seq_id` | `size_t` | 0-based index of this package within its chunk. Always 0 under `EarliestThenLongest`; increments per chain under `AllCombos`. |
| `final_package_in_chunk` | `bool` | `true` for the last package in a chunk, signalling the `ReorderBuffer` that all intra-chunk packages have been seen. |
| `chain` | `vector<WordMatch>` | Ordered, non-overlapping word matches for this chain. |

---

#### `phrase_q` — `BoundedQueue<PhrasePackage>`

**Connects:** Phrase scanner workers → Writer thread

**Purpose:** Carries grouped phrases from the phrase scanner to the single writer thread. The writer pops packages and feeds them into a `ReorderBuffer`, which reassembles them in `chunk_id` / `intra_chunk_seq_id` order before writing to disk.

**Capacity:** `queue_capacity` (default 16)

**Item type — `PhrasePackage`:**

| Field | Type | Description |
|---|---|---|
| `chunk_id` | `size_t` | Chunk index; matches the originating `ComboPackage`. |
| `intra_chunk_seq_id` | `size_t` | Intra-chunk sequence index; mirrors the `ComboPackage` value. |
| `final_package_in_chunk` | `bool` | `true` for the last package in a chunk; used by `ReorderBuffer` to drain in order. |
| `json_strs` | `vector<string>` | Pre-serialized JSON objects, one per phrase (no comma or surrounding newline). Built by the scanner worker. |

---

## Threads in Parallel Mode

The total thread count is `digit_threads + mapper_threads + finder_threads + scanner_threads + 2`: one writer thread plus the main thread, which spawns all workers and then blocks on `join()` until they finish. With all stage counts set to 1, that is 6 threads. Each stage count is set independently in the config file.

For the mapper, word finder, and phrase scanner stages, threads are managed by `StageRunner`, which spawns exactly one `std::thread` per `StageWorker` instance — so 1 worker = 1 thread. The digit feeder and writer threads are spawned directly by the pipeline without a `StageWorker` wrapper.

### Digit Feeder Threads (`digit_threads`)

**Count:** `digit_source.threads` (default 1)

Each feeder thread loops over `DigitDispatcher::next()`, which atomically claims the next chunk by incrementing a shared sequence counter and then reads the corresponding byte range from the source file via `read_at()` (using `pread`, which is safe for concurrent calls). The feeder appends the lookahead suffix (`max_word_length − 1` digit pairs), then pushes the completed `DigitPackage` to `digit_q`. When the dispatcher has no more chunks, the thread exits. The last feeder to exit calls `digit_q.set_done()`, unblocking any mapper threads waiting on the queue.

Because `pread` allows multiple threads to read different offsets of the same file descriptor simultaneously, increasing `digit_threads` can improve throughput when reading from fast storage. In practice one feeder thread is rarely the bottleneck.

### Digit Mapper Workers (`mapper_threads`)

**Count:** `digit_mapper.threads` (default 1)

Each worker loops: pop a `DigitPackage` from `digit_q`, apply `TwoDigitBlockMapper::map()` to convert digit pairs to letters, and push a `LetterPackage` to `letter_q`. The mapping is stateless and embarrassingly parallel — each package is independent. The last active mapper calls `letter_q.set_done()`.

### Word Finder Workers (`finder_threads`)

**Count:** `word_finder.threads` (default 1)

Each worker pops a `LetterPackage` from `letter_q`, runs the Aho-Corasick scan from a fresh automaton state (state is not carried across packages in parallel mode), discards matches that start in the lookahead zone, and applies the configured overlap policy:

- **`EarliestThenLongest`:** emits one `ComboPackage` per chunk.
- **`AllCombos`:** emits one `ComboPackage` per enumerated word chain, with the `final_package_in_chunk` flag set on the last one.

All resulting `ComboPackage` items are pushed to `combo_q`. The last active finder calls `combo_q.set_done()`.

Word scanning is CPU-bound and typically the most expensive stage, so `finder_threads` is the most impactful knob for performance.

### Phrase Scanner Workers (`scanner_threads`)

**Count:** `phrase_scanner.threads` (default 1)

Each worker pops a `ComboPackage` from `combo_q`, calls `HumanReviewScanner::process_words()` to group the word chain into phrases, then immediately serializes each phrase to its JSON string representation using `write_json_phrase`. The pre-built strings are stored in the `PhrasePackage` field `json_strs`. Like the mapper, each package is independent, so this stage scales linearly with thread count. The last active scanner calls `phrase_q.set_done()`.

### Writer Thread (always 1)

The writer thread pops `PhrasePackage` items from `phrase_q` and submits each one to a `ReorderBuffer`. The `ReorderBuffer` holds packages until all prior chunks (by `chunk_id` and `intra_chunk_seq_id`) have arrived, then drains them in order — ensuring that `results.json` is written in the same offset order as a serial run, regardless of which worker processed which chunk first.

Because phrases arrive pre-serialized in `json_strs`, the writer only concatenates strings to the output stream; it performs no phrase formatting or JSON construction itself.

Only one writer thread exists because disk writes must be serialized and because the `ReorderBuffer` is inherently sequential.
