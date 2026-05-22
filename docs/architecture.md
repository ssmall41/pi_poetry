# Pi Poetry — Pipeline Architecture

Pi Poetry searches the decimal expansion of π for sequences of English words encoded in consecutive digit pairs. A four-stage pipeline converts raw digits into human-readable phrases.

---

## Overview

```
FileDigitSource  →  TwoDigitBlockMapper  →  AhoCorasickCPU  →  HumanReviewScanner
 (digits 0–9)       (letters a–z)           (word matches)      (phrases)
```

The pipeline runs in either serial or parallel mode. In parallel mode, each stage runs on its own thread pool with bounded queues providing back-pressure between stages.

---

## Stage 1: Digit Source

**Purpose:** Read the raw digit file and emit fixed-size chunks of digit values to the rest of the pipeline.

**Key files:**
- [include/digit_source/DigitSource.hpp](../include/digit_source/DigitSource.hpp) — abstract interface
- [include/digit_source/FileDigitSource.hpp](../include/digit_source/FileDigitSource.hpp)
- [src/digit_source/FileDigitSource.cpp](../src/digit_source/FileDigitSource.cpp)

**Inputs:** A plain-text file containing one ASCII decimal digit per byte (e.g. `data/pi_100000000.txt`).

**Outputs:** Chunks of `uint8_t` values in the range 0–9, typically 131,072 digits per chunk. The chunk size is snapped up by the pipeline to a multiple of `digits_per_char` (2) so downstream stages always receive complete digit pairs.

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

**Purpose:** Scan the character stream for all occurrences of dictionary words using an Aho-Corasick automaton, then resolve overlapping matches according to a configured policy.

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
| `apply_etl(raw)` | Apply `EarliestThenLongest` to a raw match list. |
| `apply_all_combos_cb(raw, offset, on_chain)` | Enumerate all chains, invoking `on_chain` for each. |
| `set_min_word_length(n)` | Filter out short words before building the automaton. |

---

## Stage 4: Phrase Scanner

**Purpose:** Group consecutive word matches into phrases, tolerating small gaps of unmapped characters between words.

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
    vector<int> gap_sizes;  // character gaps between consecutive words
};
```

**Grouping algorithm (gap-tolerant mode):**

```
Sort words by start offset.
For each word:
    gap = word.start − previous_word.end
    if gap ≤ max_gap:
        extend the current phrase
    else:
        emit current phrase, start a new one
```

A gap of 0 means the words are consecutive (touching). Gaps up to `max_gap` (configurable; default 5) are accepted to allow for occasional unmapped character noise between real words.

**Streaming support:** In the parallel pipeline, phrases may span chunk boundaries. `process_words_streaming(batch, on_phrase)` accumulates a pending phrase across calls. `flush_streaming(on_phrase)` emits the final pending phrase at end-of-stream.

**Output writing:**

| Method | Description |
|---|---|
| `write_results(phrases, run_dir)` | Write `results.txt` and `results.json` to the run directory. |
| `write_text_phrase(out, phrase)` | Format a single phrase as a human-readable text line. |
| `write_json_phrase(out, phrase)` | Format a single phrase as a JSON object. |

---

## Additional Major Components

### Pipeline Orchestrator

**Key files:**
- [include/pipeline/Pipeline.hpp](../include/pipeline/Pipeline.hpp)
- [src/pipeline/Pipeline.cpp](../src/pipeline/Pipeline.cpp)

The `Pipeline` class wires the four stages together and drives execution in one of two modes.

**Serial mode (`run()`):** A single thread reads chunks sequentially, maps them, scans for words (carrying the AC state across chunks), groups into phrases with the streaming phrase scanner, and writes results in order.

**Parallel mode (`run_parallel()`):** Each stage runs on a configurable thread pool. Stages communicate via `BoundedQueue<T>`, which blocks producers when full, providing automatic back-pressure.

```
DigitDispatcher
      │  DigitPackage
      ▼
DigitMapperWorker × N
      │  LetterPackage
      ▼
WordFinderWorker × N
      │  ComboPackage (one per word chain)
      ▼
PhraseScannerWorker × N
      │  PhrasePackage
      ▼
Writer Thread (ReorderBuffer)
      │
      ▼
results.txt / results.json
```

Key infrastructure classes:

| Class | Role |
|---|---|
| `DigitDispatcher` | Assigns non-overlapping chunks to feeder threads via an atomic sequence counter; uses `read_at` for concurrent random access. Each chunk includes a lookahead suffix for boundary-spanning words. |
| `BoundedQueue<T>` | Thread-safe FIFO with configurable capacity; back-pressure via condition variables. |
| `StageWorker<In, Out>` | Abstract base for mapper, word finder, and phrase scanner workers. |
| `StageRunner<In, Out>` | Launches N worker threads, each looping over `pop → process → emit`. |
| `ReorderBuffer<T>` | Two-level buffer (by chunk id, then intra-chunk sequence id) that drains complete chunks in offset order, restoring deterministic output despite out-of-order parallel processing. |

### Configuration & Validation

**Key files:**
- [config/default.toml](../config/default.toml)
- [include/config_validator.hpp](../include/config_validator.hpp)
- [src/config_validator.cpp](../src/config_validator.cpp)

All parameters are specified in a TOML config file passed via `--config`. The validator checks file existence, reserved field values, thread counts, gap bounds, and policy names before the pipeline starts. Any error causes an immediate exit with a descriptive message.

Major config sections:

| Section | Key parameters |
|---|---|
| `[pipeline]` | `mode` (`serial`/`parallel`), `debug`, `dry_run` |
| `[digit_source]` | `path`, `chunk_size`, `threads` |
| `[digit_mapper]` | `write_letter_sequence` |
| `[word_finder]` | `dictionary`, `overlap_policy`, `min_word_length`, `threads` |
| `[phrase_scanner]` | `max_gap`, `threads` |
| `[analysis]` | `run_after_pipeline` |

### Result Analyzer

**Key files:**
- [include/result_analyzer/](../include/result_analyzer/)
- [src/result_analyzer/](../src/result_analyzer/)

A post-processing pass that reads `results.json` from a completed run and produces:

- `phrases_length_N.txt` — one file per distinct phrase length, listing offset and words.
- `statistics.txt` — phrase counts by length and top-10 longest words found.

The analyzer can run automatically after the pipeline (`analysis.run_after_pipeline = true`) or be invoked standalone via `./build/analyze_results <run_dir>`.

### Pi Downloader Utility

**Binary:** `./build/pi_download <num_digits> [--output <path>]`

Fetches digits from the pi.delivery API in 1,000-digit batches and writes a plain-text digit file suitable for use as the digit source.

---

## Output Files

Each pipeline run produces a timestamped directory under `outputs/`:

| File | Content |
|---|---|
| `results.txt` | Plain-text phrases: `Offset 12345: word1 word2 [gaps: 0 2]` |
| `results.json` | JSON array of `{start_offset, words, gap_sizes}` objects |
| `letter_sequence.txt` | Full mapped character sequence (optional, for debugging) |
| `phrases_length_N.txt` | Phrases of exactly N words (if analysis enabled) |
| `statistics.txt` | Phrase count summary and top-10 longest words (if analysis enabled) |
