# Pi Poetry
## Technical Design Document
*Version 0.3 — Final | Status: Implemented*

### **Note: this TDD was used to define the MVP. The current project is past this point. The information here is kept for posterity.**

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Goals and Non-Goals](#2-goals-and-non-goals)
3. [System Overview](#3-system-overview)
4. [Stage 1: Digit Source](#4-stage-1-digit-source)
5. [Stage 2: Digit Mapper](#5-stage-2-digit-mapper)
6. [Stage 3: Word Finder](#6-stage-3-word-finder)
7. [Stage 4: Phrase Scanner](#7-stage-4-phrase-scanner)
8. [MVP Summary](#8-mvp-summary)
9. [Technical Specifications](#9-technical-specifications)
10. [Suggested Project Structure](#10-suggested-project-structure)
11. [Open Questions and Future Work](#11-open-questions-and-future-work)
12. [Revision History](#12-revision-history)

---

## 1. Introduction

The number pi is believed to be normal, meaning that in its decimal expansion every finite sequence of digits appears somewhere. You can take your birthday — that is a finite number and it appears somewhere in pi. Take your birthday concatenated with your parents' birthdays; that is also a finite number and it too appears in pi. Take the script from your favorite movie, map each letter to a two-digit number, and you produce a long but finite sequence of digits that appears in pi. Take the complete works of William Shakespeare, map those letters to numbers, and that finite sequence of digits appears somewhere in pi.

There are programs and services that take a finite sequence of digits and try to find where it appears in pi. Pi Poetry reverses that process: given an encoding of digits to letters, what phrases, sentences, or poetry can be found among the digits of pi?

Pi Poetry is a high-performance software system that searches for natural language words and phrases hidden within the decimal expansion of pi — and optionally other infinite or finite numerical sequences. The fundamental insight motivating this project is that any sufficiently long sequence of digits, when mapped to characters, will statistically contain an enormous number of recognizable words and, with enough digits, coherent phrases.

This document is organized in two layers. Sections 3 through 7 describe each pipeline stage in full, covering both the MVP implementation and all planned future variants. Section 8 then summarizes the MVP in one place for quick reference. Sections 9 onward cover technical specifications, project structure, open questions, and revision history.

This document is intended as the primary reference for implementation by the development team and for automated code-generation tools such as Claude Code.

---

## 2. Goals and Non-Goals

### 2.1 Goals

- Implement a complete, end-to-end pipeline for discovering words and phrases in digit sequences.
- Provide high-throughput processing via multi-threading, with optional GPU (CUDA) acceleration as a future extension.
- Expose a modular, plugin-style architecture so that individual pipeline stages can be independently swapped, benchmarked, and extended.
- Support multiple digit sources, character mappings, natural languages, and phrase-identification strategies.
- Allow all four pipeline stages to run in parallel (pipelined execution), as well as in serial mode for reproducibility and debugging.

### 2.2 Non-Goals

- Pi Poetry is not a mathematical proof system; it does not claim any novel mathematical result about the normality of pi.
- A graphical user interface is out of scope.
- Real-time streaming of newly computed digits is not required.

---

## 3. System Overview

The system is decomposed into four sequential but parallelizable pipeline stages:

| Stage | Responsibility |
|---|---|
| 1 — Digit Source | Produce a sequence of digits from pi or another source. |
| 2 — Digit Mapper | Convert the digit sequence into a character sequence. |
| 3 — Word Finder | Scan the character sequence and extract candidate words from a dictionary. |
| 4 — Phrase Scanner | Analyze the word stream and identify sentences or meaningful phrases. |

Each stage communicates with the next through a well-defined C++ abstract base class (interface). Concrete implementations are selected at startup via a TOML configuration file and/or command-line flags, allowing users to mix and match components without recompiling.

The pipeline may run in **serial mode** (each stage completes fully before the next begins) or in **pipelined mode**, where each stage runs in its own thread and passes work through lock-free queues. Serial mode is recommended for debugging and for verifying correctness.

---

## 4. Stage 1: Digit Source

### 4.1 Responsibility

The Digit Source stage produces a (potentially infinite) stream of digits. Downstream stages consume digits in configurable chunk sizes.

### 4.2 Interface — DigitSource

- `next_chunk(buffer, n)` — fills the buffer with the next n digits; returns the number of digits actually written (may be fewer at end-of-stream).
- `reset()` — resets the source to the beginning of the sequence.
- `is_finite() -> bool` — indicates whether the source has a known end.
- `estimated_length() -> std::optional<uint64_t>` — returns total digit count if known.
- `base() -> int` — returns the numeric base of the digits produced (e.g., 10 for decimal, 4 for the genome encoding). The Digit Mapper uses this to validate and select an appropriate mapping scheme.

### 4.3 Implementations

#### 4.3.1 File-Based Source (MVP)

Reads pre-computed digits of pi from a user-supplied local file. Files may be in plain decimal text format (one ASCII digit character per byte) or a compact binary nibble-packed format.

- Supported formats: plain text (`.txt`), nibble-packed binary (`.bin`).
- Configurable read-ahead buffer size.
- Thread-safe: multiple reader threads may request chunks concurrently via an internal mutex.
- No digit file is bundled with the application. Users supply the path in the configuration file. The `data/` directory in the repository is reserved for such files and is listed in `.gitignore`.

> **MVP:** File-Based Source is the MVP implementation. Users must supply a digit file.

#### 4.3.2 HTTP Source (Future)

Downloads pre-computed digits from a public web service. Useful for bootstrapping without a local file.

- [pi.delivery](https://api.pi.delivery/v1/pi) provides up to 1,000 digits per request and is the recommended default endpoint. Larger digit files should be sourced from archives such as the y-cruncher digit repository.
- Chunked HTTP download; resumes from a byte offset on failure.
- Local disk cache to avoid redundant downloads.

> **Future:** HTTP Source is a planned future feature.

#### 4.3.3 On-the-Fly Computation Source (Future)

Computes digits of pi directly using a spigot algorithm or a fast series-expansion algorithm (e.g., the Chudnovsky algorithm).

- Single-threaded reference implementation using the GMP or MPFR library.
- Optional GPU-accelerated variant using CUDA for massively parallel big-integer arithmetic.

> **Future:** On-the-fly computation is a planned future feature.

#### 4.3.4 Alternative Sequence Sources (Future)

The same interface supports other digit sequences as drop-in replacements:

- Other mathematical constants: e, sqrt(2), the Feigenbaum constant, etc.
- The human genome (A/C/G/T encoded as 0–3, base 4).
- Arbitrary user-supplied digit files in any supported base.
- Truncated or windowed views of any source.

> **Future:** Alternative sources are planned future features.

---

## 5. Stage 2: Digit Mapper

### 5.1 Responsibility

The Digit Mapper converts a stream of digits into a stream of characters. A mapping consumes one or more consecutive digits and produces one character. The mapping scheme and the output alphabet are independently configurable.

### 5.2 Interface — DigitMapper

- `map(digits, n_digits, out_chars, out_n)` — converts up to n_digits digits into characters written to out_chars; sets out_n to the number of characters produced.
- `digits_per_char() -> int` — number of input digits consumed per output character (e.g., 1 or 2).
- `alphabet_size() -> size_t` — number of distinct output characters.
- `alphabet() -> std::string_view` — the set of output characters.
- `required_base() -> int` — the numeric base this mapper expects its digits to be in. The pipeline checks this against `DigitSource::base()` at startup and raises an error on mismatch.

### 5.3 Numeric Base

The digit sequence produced by the Digit Source is not necessarily base 10. The mapper declares the base it expects, enabling future support for non-decimal sources (e.g., a base-4 genome sequence). For the MVP, base 10 is assumed throughout.

> **MVP:** Base 10 is assumed. The `required_base()` check is implemented but only base-10 sources and mappers are provided in the MVP.

### 5.4 Mapping Schemes

#### 5.4.1 Two-Digit Block Mapping (MVP)

Pairs of consecutive decimal digits (00–99) are mapped to a 26-character lowercase English alphabet as follows:

| Digit Pair Range | Mapped Letters | Notes |
|---|---|---|
| 00 – 25 | a – z | First full cycle |
| 26 – 51 | a – z | Second full cycle |
| 52 – 77 | a – z | Third full cycle |
| 78 – 99 | a – v | Partial fourth cycle; 'w'–'z' are not reachable |

Each pair of digits `d` is mapped to the letter at alphabet position `d mod 26`. The pairs 00–25, 26–51, and 52–77 each cover the full alphabet exactly once; the remaining pairs 78–99 cover only letters a through v (22 letters, since 99 mod 26 = 21 → 'v'). This introduces a slight statistical bias against the letters w, x, y, and z, which is acceptable for the MVP.

> **MVP:** Two-Digit Block Mapping with the 26-character lowercase English alphabet is the MVP.

#### 5.4.2 Rejection-Sampling Mapping (Future)

Digit pairs or triples are consumed until a value falls within a range that divides evenly into the alphabet size, discarding values outside that range. This produces a perfectly uniform distribution over the alphabet at the cost of variable (and slightly higher) digit consumption.

> **Future:** Rejection-sampling mapping is a planned future feature.

#### 5.4.3 Prolific Mapping Search (Future)

Instead of a fixed formula, this scheme searches for the digit-to-letter mapping that maximizes the number of dictionary words found in a fixed-length prefix of the digit sequence (e.g., the first one million digits of pi). The search treats the mapping as an optimization problem:

- Enumerate (or sample) permutations of the alphabet assignment.
- For each candidate mapping, run the Word Finder on the truncated digit sequence and count distinct words found.
- Return the mapping with the highest word count as the recommended encoding.

This approach can be combined with any of the other mapping schemes by treating the scheme as a parameterized family and hill-climbing over the parameter space. Because an exhaustive search over all 26! permutations is infeasible, practical implementations will use greedy search, simulated annealing, or a genetic algorithm.

> **Future:** Prolific Mapping Search is a planned future feature.

### 5.5 Configurable Alphabet Presets

| Preset Name | MVP / Future | Character Set |
|---|---|---|
| alpha-lower (MVP) | MVP | a–z (26 characters) |
| alpha-mixed | Future | a–z, A–Z (52 characters) |
| alpha-numeric | Future | a–z, A–Z, 0–9 (62 characters) |
| printable | Future | All 95 printable ASCII characters |
| custom | Future | User-supplied string of distinct characters |

---

## 6. Stage 3: Word Finder

### 6.1 Responsibility

The Word Finder scans the character stream produced by the Digit Mapper and identifies substrings that are valid words in the chosen natural language. It produces a stream of `WordMatch` records, each containing the matched word, its start position in the character stream, its length, and whether it is directly adjacent to the previous word or separated by unused characters.

### 6.2 Interface — WordFinder

- `scan(char_buffer, offset) -> vector<WordMatch>` — scans the buffer starting at the given global character offset and returns all word matches.
- `load_dictionary(path)` — loads the word list for the selected language.
- `set_overlap_policy(policy)` — configures the overlap resolution strategy.

The `WordMatch` record contains:

- `word` — the matched string.
- `start` — global character offset where the word begins.
- `length` — number of characters in the word.
- `consecutive` — bool: `true` if this word begins exactly where the previous selected word ended (no intervening unused characters); `false` if there is a gap.

### 6.3 Dictionary and Language Selection

Word Finder implementations accept a dictionary file in one-word-per-line plain text format. The dictionary is loaded into an Aho-Corasick automaton for O(n) scanning. Planned language support:

- English (MVP)
- German, French, Spanish, Italian (future)
- User-supplied custom word list

> **MVP:** English dictionary, minimum word length 3 characters. The dictionary ships with the application (approximately 170,000 words).

### 6.4 Overlap Resolution Policies

A single position in the character stream may be the start of multiple words, and words may overlap with one another. For example, in the character sequence `"qbswordslp"` the substrings `"word"`, `"words"`, `"sword"`, and `"swords"` all appear. The two supported policies differ in which of these matches are forwarded to Stage 4.

#### 6.4.1 Earliest-Then-Longest (MVP)

The scanner advances left-to-right. At each position it checks whether any dictionary word starts here. If one or more words start at the earliest position that has a match, it selects the longest among them, emits it, and advances past the end of that word before looking for the next match. Characters between selected words are not emitted.

Example — character sequence: `"qbswordslp"`

- `"sword"` starts at position 2 (length 5) and `"swords"` starts at position 2 (length 6). `"word"` starts at position 3 (length 4) and `"words"` starts at position 3 (length 5).
- Earliest start position with a match is 2. Longest word starting at 2 is `"swords"` (length 6).
- `"swords"` is emitted. The scanner advances to position 8, finds no further words, and halts.
- Result: `["swords"]`

> **MVP:** Earliest-Then-Longest is the MVP overlap policy.

#### 6.4.2 All-Starts

For each start position that has at least one match, only the longest word at that position is emitted. Multiple emitted words may overlap if they begin at different positions.

Example — character sequence: `"qbswordslp"`

- Position 2: longest match is `"swords"` (length 6). Emit `"swords"`.
- Position 3: longest match is `"words"` (length 5). Emit `"words"`.
- Result: `["swords" at 2, "words" at 3]`. These two words overlap.

| Policy | Description |
|---|---|
| earliest-then-longest (MVP) | Greedy left-to-right; at each step select the longest word at the earliest available start position; advance past it. |
| all-starts | Emit the longest word at every start position that has a match; overlapping words are permitted. |

### 6.5 Implementation Variants

#### 6.5.1 CPU Single-Threaded (MVP Reference)

A straightforward Aho-Corasick implementation. Used for correctness validation and as a baseline for performance comparison.

> **MVP:** CPU Single-Threaded Aho-Corasick is the MVP implementation.

#### 6.5.2 CPU Multi-Threaded (Future)

The character buffer is partitioned into overlapping chunks (overlap = maximum word length − 1 characters). Each chunk is processed by a thread from a configurable thread pool. Results are merged and de-duplicated by position.

> **Future:** CPU multi-threaded scanning is a planned future feature.

#### 6.5.3 GPU / CUDA (Future)

Each CUDA thread processes one character position, checking all dictionary entries that could start at that position using a device-resident finite-state machine. Results are written to a device-side output buffer and copied back asynchronously.

> **Future:** GPU/CUDA scanning is a planned future feature.

---

## 7. Stage 4: Phrase Scanner

### 7.1 Responsibility

The Phrase Scanner analyzes the stream of `WordMatch` records from the Word Finder and identifies sequences of words that form coherent sentences or meaningful phrases.

### 7.2 Interface — PhraseScanner

- `process_words(word_stream) -> vector<PhraseMatch>` — consumes a word stream and returns candidate phrases.
- `set_gap_policy(policy)` — configures how gaps between words are handled.

### 7.3 Gap Policies

#### 7.3.1 Strict (Consecutive Words Only)

Only word sequences where every word is directly adjacent to the next (i.e., `WordMatch::consecutive == true` for all words after the first) are considered phrases. No unused characters are allowed between words.

Example: suppose the two-digit block mapping produces the character sequence `"...catdog..."` at some offset. In strict mode these two words form a phrase candidate only if `"dog"` begins at exactly the character position where `"cat"` ends.

#### 7.3.2 Gap-Tolerant (MVP)

Word sequences where up to G unused characters appear between consecutive words are still considered phrases. G is a configurable parameter (default: 5). The `WordMatch::consecutive` flag is `false` for word transitions that cross a gap.

Example: with G = 3 and the character sequence `"catXXdog"` (where `XX` represents two unused characters), `"cat"` and `"dog"` are separated by 2 unused characters (2 ≤ 3), so they form a phrase candidate. With `"catXXXXdog"` (4 unused characters, 4 > 3), they do not.

| Mode | Gap Allowed | Example |
|---|---|---|
| strict | 0 characters | "cat" ends at pos 5; "dog" must start at pos 5. |
| gap-tolerant (MVP) | 0 to G chars (default G=5) | "cat" at pos 0–2, "dog" at pos 5–7 — gap of 2, within limit. |

### 7.4 Output Format

The Phrase Scanner outputs all word sequences of length ≥ 2, sorted by the character offset where the first word in the sequence begins (ascending). For each sequence the output includes:

- The starting character offset in the pi digit expansion.
- The sequence of words, in order.
- The gap sizes between consecutive words (0 for strictly adjacent, >0 for gap-tolerant matches).
- A formatted plain-text report and a parallel JSON file for programmatic consumption.

> **MVP:** Output is sorted by starting offset. Human review is the MVP identification method.

### 7.5 Identification Methods

#### 7.5.1 Manual Human Review (MVP)

The Phrase Scanner produces a formatted report and a JSON export. A human reviewer reads through the output and judges which word sequences form meaningful phrases or sentences.

> **MVP:** Manual Human Review is the MVP method.

#### 7.5.2 LLM-Assisted Review (Future)

Word sequences are sent to a large language model (e.g., via the Anthropic Claude API) with a prompt asking it to rate the phrase for naturalness, grammatical correctness, and meaning. High-scoring phrases are surfaced automatically.

- Batch API calls to minimize latency and cost.
- Configurable scoring threshold.
- Local LLM option (e.g., llama.cpp) to avoid API costs and network dependency.

> **Future:** LLM-assisted review is a planned future feature.

#### 7.5.3 Statistical Grammar Filter (Future)

A lightweight n-gram language model assigns a log-probability to each word sequence. Sequences exceeding a perplexity threshold are discarded; the rest are ranked and presented to the user.

> **Future:** Statistical grammar filter is a planned future feature.

---

## 8. MVP Summary

The table below summarizes the MVP implementation choice for each stage. All other variants described in Sections 4–7 are future work.

| Stage | MVP Implementation |
|---|---|
| 1 — Digit Source | File-Based Source. User supplies a plain-text file of decimal digits of pi. No file is bundled. |
| 2 — Digit Mapper | Two-Digit Block Mapping over 26 lowercase English letters. Digit pairs 00–99 map to a–y/z with slight bias against z. Base 10. |
| 3 — Word Finder | CPU single-threaded Aho-Corasick, English dictionary, earliest-then-longest overlap policy, minimum word length 3. |
| 4 — Phrase Scanner | Gap-tolerant mode (G = 5), output sorted by starting offset, manual human review. Plain-text and JSON output. |

---

## 9. Technical Specifications

### 9.1 Language and Compiler

- Primary language: C++23.
- Compiler: GCC 13+ only.
- Build system: CMake 3.28+.
- CUDA Toolkit 12.x for GPU-accelerated components (future; not required for MVP build).

> **MVP:** The MVP build has no GPU dependency. CUDA support is compiled in only when the CUDA toolkit is detected by CMake.

### 9.2 Execution Modes

#### 9.2.1 Serial Mode (MVP)

Each pipeline stage runs to completion before the next stage begins. Stage 1 writes all digits to disk or memory; Stage 2 reads them and writes all characters; Stage 3 reads all characters and writes all word matches; Stage 4 reads all word matches and writes the report. Serial mode is the recommended mode for the MVP: it is straightforward to implement, easy to debug, and fully reproducible.

> **MVP:** Serial mode is the MVP execution mode.

#### 9.2.2 Pipelined Mode (Future)

Each stage runs in its own thread and communicates with the next through lock-free single-producer / single-consumer ring buffers. This allows all four stages to run concurrently, overlapping I/O, computation, and output.

- Back-pressure is applied when a downstream queue is full, preventing memory exhaustion.
- Each stage has a configurable number of worker threads (see 9.3).

> **Future:** Pipelined execution is a planned future feature.

### 9.3 Thread Configuration

When pipelined mode is enabled, each stage has an independently configurable thread count in the configuration file:

| Configuration Key | MVP Default | Description |
|---|---|---|
| `digit_source.threads` | 1 | Threads for the Digit Source stage. |
| `digit_mapper.threads` | 1 | Threads for the Digit Mapper stage. |
| `word_finder.threads` | 1 | Threads for the Word Finder stage. Values > 1 use chunk-based parallelism. |
| `phrase_scanner.threads` | 1 | Threads for the Phrase Scanner stage. |

Setting any value to 1 (the default) causes that stage to run on its pipeline thread only, with no additional parallelism within the stage. In serial mode, all thread counts are ignored and each stage runs single-threaded.

> **MVP:** All thread counts are 1 in the MVP. Thread configuration keys are present in the schema for forward compatibility.

### 9.4 GPU / CUDA (Future)

GPU-accelerated implementations of the Digit Source (on-the-fly computation) and Word Finder are planned as optional future components. All GPU code will be isolated in separate `.cu` translation units and compiled only when CUDA is available. The pipeline's interface layer will be unchanged: a GPU-backed Word Finder satisfies the same `WordFinder` interface as the CPU variant.

> **Future:** No GPU code is included in the MVP. GPU support is a planned future feature.

### 9.5 Sample Configuration File

```toml
[pipeline]
mode = "serial"               # "serial" or "pipelined"

[digit_source]
type = "file"
path = "data/pi_digits.txt"
threads = 1

[digit_mapper]
type = "two-digit-block"
alphabet = "alpha-lower"
base = 10
threads = 1

[word_finder]
type = "aho-corasick-cpu"
language = "english"
overlap_policy = "earliest-then-longest"
min_word_length = 3
threads = 1

[phrase_scanner]
type = "human-review"
mode = "gap-tolerant"
max_gap = 5
threads = 1
output_text = "results.txt"
output_json = "results.json"
```

### 9.6 Performance Targets

| Metric | Target |
|---|---|
| Digit ingestion throughput (file) | >= 500 MB/s on NVMe SSD. |
| Character mapping throughput (CPU, 1 thread) | >= 200 million characters/second. |
| Word scanning throughput (CPU, 1 thread) | >= 50 million characters/second. |
| Word scanning throughput (CPU, 8 threads — future) | >= 100 million characters/second. |
| GPU word scanning throughput (future) | >= 1 billion characters/second (single A100 / RTX 4090). |
| Memory footprint (1 billion digits) | < 4 GB RAM. |

### 9.7 Dependencies

| Library | MVP / Future | Purpose |
|---|---|---|
| C++ standard library (C++23) | MVP | Core language features. |
| nlohmann/json | MVP | JSON output. |
| toml++ | MVP | Configuration file parsing. |
| Google Test / Catch2 | MVP | Unit testing. |
| Google Benchmark | MVP | Micro-benchmarking. |
| oneTBB or std::execution | Future | Thread pool for pipelined mode. |
| CUDA Toolkit 12.x | Future | GPU-accelerated components. |
| GMP / MPFR | Future | On-the-fly pi computation. |
| libcurl | Future | HTTP digit source. |

---

## 10. Suggested Project Structure

```
pi_poetry/
├── CMakeLists.txt
├── config/
│   └── default.toml
├── data/                      # Place digit files here (not tracked by git)
│   └── .gitkeep
├── dictionaries/
│   └── english.txt
├── include/
│   ├── digit_source/
│   │   └── DigitSource.hpp
│   ├── digit_mapper/
│   │   └── DigitMapper.hpp
│   ├── word_finder/
│   │   └── WordFinder.hpp
│   └── phrase_scanner/
│       └── PhraseScanner.hpp
├── src/
│   ├── digit_source/
│   │   ├── FileDigitSource.cpp
│   │   ├── HttpDigitSource.cpp        # Future
│   │   └── ComputeDigitSource.cu      # Future
│   ├── digit_mapper/
│   │   ├── TwoDigitBlockMapper.cpp
│   │   └── ProlificMapperSearch.cpp   # Future
│   ├── word_finder/
│   │   ├── AhoCorasickCPU.cpp
│   │   └── AhoCorasickGPU.cu          # Future
│   ├── phrase_scanner/
│   │   ├── HumanReviewScanner.cpp
│   │   └── LLMScanner.cpp             # Future
│   ├── pipeline/
│   │   └── Pipeline.cpp
│   └── main.cpp
└── tests/
    ├── test_digit_source.cpp
    ├── test_digit_mapper.cpp
    ├── test_word_finder.cpp
    └── test_phrase_scanner.cpp
```

---

## 11. Open Questions and Future Work

- What is the best publicly available source for pre-computed pi digits, and what format (plain text vs. binary nibble-packed) should be preferred?
- Should the Digit Mapper support non-uniform mappings (e.g., Huffman-coded alphabets that give all characters equal frequency in the output)?
- What is the optimal minimum word length for the Word Finder? Shorter words produce more candidates but more noise.
- For the LLM-assisted Phrase Scanner: should the system use a local model or a cloud API? What is the acceptable latency and cost trade-off?
- Should the pipeline support checkpointing so that a scan of billions of digits can be resumed after a crash or machine restart?

---

## 12. Revision History

| Version | Date | Author | Notes |
|---|---|---|---|
| 0.1 | 2026-05-05 | — | Initial draft based on project brief. |
| 0.2 | 2026-05-05 | — | American English; separated project overview from MVP; removed bundled digit file; renamed interfaces (dropped I-prefix); revised Digit Mapper MVP to two-digit block scheme; added prolific mapping and base-agnostic notes; removed all-overlapping policy; added policy examples; added consecutive flag to WordFinder; phrase output sorted by offset with examples; per-stage thread config; serial mode documented; GPU deferred to future; trimmed Open Questions. |
| 0.3 | 2026-05-05 | — | Removed Single-Digit Modular Mapping; removed All-Non-Overlapping policy; compiler restricted to GCC 13+; removed Ninja build backend; removed radix trie mention; added pi.delivery as HTTP source. |
