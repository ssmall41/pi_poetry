# Pi Poetry

Pi Poetry searches for natural-language words and phrases hidden in large sequences of digits. It maps consecutive-digit to letters and scans the resulting character stream with an [Aho-Corasick](https://en.wikipedia.org/wiki/Aho%E2%80%93Corasick_algorithm) automaton, then groups found words into phrases.

## Building

**Requirements:** GCC 13+, CMake 3.28+, Python 3 (used once at configure time to compile cpp-httplib as a library). Dependencies (nlohmann/json, toml++, cpp-httplib, Google Test) are downloaded automatically via CMake FetchContent.

**Debug build** (development, includes the test binary):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```

**Release build** (optimized `-O3`, for real runs over large digit files):

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-O3
cmake --build build_release --parallel 4
```

The optimized programs are then at `build_release/pi_poetry`, `build_release/pi_download`, etc. The default build also compiles the test binary; to build **only** the production programs (the bulk of the release build time is the test binary, so this is much faster), restrict the targets:

```bash
cmake --build build_release --parallel 4 --target pi_poetry pi_download gen_mapping analyze_results
```

**Build performance / memory.** The build is dominated by heavyweight header-only
dependencies (`nlohmann/json`, `cpp-httplib`), so a few measures keep it fast and
keep the machine usable while compiling:

- **`--parallel 4`** (not bare `--parallel`): each heavy `-O3` translation unit can
  need ~0.3–1.5 GiB, so eight at once will thrash a memory-constrained machine
  into swap. Cap to ~4 (use `--parallel 3` if you still swap).
- **Install `ccache`** (`sudo apt install ccache`) — CMake auto-detects it and routes
  all compiles through it, so unchanged translation units (including the one-time
  Google Test compile) are served from cache on rebuilds and branch switches with no
  recompile and no memory cost. For precompiled-header compatibility, add
  `export CCACHE_SLOPPINESS=pch_defines,time_macros` to your shell rc.
- The test binary uses a **precompiled header** for `gtest`/`json`, and `cpp-httplib`
  is compiled **once** into a static library instead of inlined per TU — both happen
  automatically, no action needed.
- **gmock is not built** (only `gtest_main` is linked) and the third-party deps
  (`cpp-httplib`, Google Test) are compiled with `-g0` — we never debug into them.
  These cut total compile work and lower peak memory (~728 → ~620 MiB for a Debug
  clean build).

## Running

### Reading digits from a text file

Supply a plain-text file of digits (one ASCII digit per byte) and adjust `config/default.toml` to point at your file. Then:

```bash
./build/pi_poetry --config config/default.toml
```

Each run creates a timestamped subfolder (e.g. `outputs/run-20260506_143022/`) containing `results.json`. If `write_letter_sequence = true` in the `[digit_mapper]` config, `letter_sequence.txt` is also written. The output directory is configurable in `config/default.toml`.

### Downloading digits of pi from an online API

Instead of a local file, you can stream digits of pi directly from an HTTP/HTTPS API. Set `digit_source.type = "api"` and point `source_config` at a per-source TOML file that describes the API. A ready-made config for [api.pi.delivery](https://api.pi.delivery) is included:

```toml
[digit_source]
type          = "api"
source_config = "config/sources/pi_delivery.toml"
max_digits    = 100000   # 0 = run indefinitely
threads       = 4
```

Multiple threads download different chunks in parallel (each request specifies a start offset). The `api.pi.delivery` API allows up to 1,000 digits per request; the pipeline issues as many requests as needed to fill each chunk.

### Graceful stop (indefinite runs)

When `max_digits = 0` the pipeline runs until interrupted. To stop gracefully — letting in-flight chunks finish before the pipeline drains and exits — create a file named `pi_poetry.stop` in the working directory:

```bash
touch pi_poetry.stop
```

The pipeline detects this file between chunks, stops fetching new data, drains the remaining stages, and deletes `pi_poetry.stop` when done.

## Running Tests

```bash
ctest --test-dir build --output-on-failure
```

## Configuration

The configuration file is a [TOML](https://toml.io/) document passed via `--config`. See `config/default.toml` for a complete example.

### `[output]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `dir` | `"outputs"` | Any directory path | Base directory for run output. Each run creates a `run-YYYYMMDD_HHMMSS` subfolder here containing all output files. |

### `[pipeline]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `mode` | `"serial"` | `"serial"`, `"parallel"` | Execution mode. `"serial"` runs all stages on one thread. `"parallel"` runs each stage with a pool of worker threads connected by bounded queues; the number of workers per stage is set by the `threads` field in each stage's section. |
| `debug` | `false` | `true`, `false` | When true, prints a `[stage] worker N claimed package M (in: K remaining, out: Y pending)` message to stdout each time a worker picks up a work package. `in` is the number of packages still waiting in that stage's input queue; `out` is the number already queued for the next stage. Useful for observing parallelism and back-pressure; leave false for clean output in production runs. |
| `dry_run` | `false` | `true`, `false` | When true, output is written to `/dev/null` instead of the run directory. Use this to benchmark pipeline throughput without I/O overhead. |

### `[digit_source]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` | `"file"` | `"file"`, `"api"` | Digit source implementation to use. `"file"` reads from a local plain-text file; `"api"` downloads from an HTTP/HTTPS API described by `source_config`. |
| `path` | `"data/pi_2000.txt"` | Any file path | *(type = "file" only)* Plain-text file of pi digits, one ASCII digit per byte. |
| `source_config` | — | Any file path | *(type = "api" only)* Path to a per-source TOML file describing the API endpoint (see `config/sources/pi_delivery.toml` for an example). |
| `max_digits` | `0` | Integer ≥ 0 | Maximum digits to process. `0` means no cap (reads the full file, or runs indefinitely for API sources). Applies to both `"file"` and `"api"` sources. |
| `threads` | `1` | Positive integer | Number of worker threads that read digit chunks from the source and push them into the pipeline. For `"api"` sources, each thread downloads independently. |
| `chunk_size` | `131072` | Positive integer | Number of digits each worker reads per work package. Values that are not a multiple of `digits_per_char` (2 for the default mapper) are silently rounded up. Larger chunks reduce coordination overhead; smaller chunks increase parallelism granularity. |
| `queue_size` | `16` | Positive integer | Maximum number of digit packages buffered between the digit source and the digit mapper. The digit source blocks when this limit is reached, applying back-pressure. |

### `[digit_mapper]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` | `"two-digit-block"` | `"two-digit-block"`, `"mapping-file"` | Mapper implementation to use. `"two-digit-block"` uses the built-in encoder (hardcoded: base 10, alphabet a–z); `"mapping-file"` loads the encoding from a plain-text file specified by `mapping_file`. |
| `mapping_file` | — | Any file path | *(type = "mapping-file" only)* Path to the mapping file (relative to the working directory). See below for the file format. |
| `threads` | `1` | Positive integer | Number of worker threads that convert digit packages to letter packages. |
| `write_letter_sequence` | `false` | `true`, `false` | When true, writes the mapped letter sequence to `letter_sequence.txt` in the run directory. |
| `queue_size` | `16` | Positive integer | Maximum number of letter packages buffered between the digit mapper and the word finder. The digit mapper blocks when this limit is reached, applying back-pressure. |

#### Mapping file format (`type = "mapping-file"`)

A plain-text file with two header lines followed by one entry per line:

```
# Comments start with #; blank lines are ignored
digits_per_char=2
base=10
00 a
01 b
...
99 v
```

- **`digits_per_char`** — how many consecutive input digits produce one output character (must be ≥ 1).
- **`base`** — numeric base of the digit stream (2–10). Must match the digit source.
- **Entry lines** — `<combo> <char>`: the digit combination (exactly `digits_per_char` decimal digits, each in range 0–base−1) followed by a space and a single output character.
- Every possible combination must appear exactly once (no missing, no duplicates). The mapper fails at startup with a descriptive error if the file is invalid.

`config/mappings/two_digit_block.txt` is a ready-made mapping file that produces identical output to the built-in `"two-digit-block"` encoder.

### `[word_finder]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"aho-corasick-cpu"` | `"aho-corasick-cpu"` | Word-finder implementation to use. |
| `dictionary` | `"dictionaries/english.txt"` | Any file path | Path to the word list, one word per line. |
| `overlap_policy` | `"earliest-then-longest"` | `"earliest-then-longest"`, `"all-combos"` | How to resolve overlapping word matches. `"earliest-then-longest"` greedily picks one non-overlapping sequence (the match starting soonest, ties broken by longest word), then splits it into runs of directly consecutive words. `"all-combos"` enumerates every possible consecutive chain of non-overlapping words; use this to explore all valid readings. Under both policies, only runs of at least `[phrase_scanner].min_phrase_length` consecutive words are emitted. |
| `min_word_length` | `1` | Positive integer | Words shorter than this are ignored when loading the dictionary. |
| `threads` | `1` | Positive integer | Number of worker threads that scan letter packages for dictionary words. |
| `queue_size` | `16` | Positive integer | Maximum number of combo packages buffered between the word finder and the phrase scanner. The word finder blocks when this limit is reached, applying back-pressure. |

### `[phrase_scanner]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"human-review"` | `"human-review"` | Phrase-scanner implementation to use. |
| `min_phrase_length` | `1` | Positive integer | Minimum number of words a phrase must contain to be included in the output. Phrases shorter than this are silently discarded. |
| `threads` | `1` | Positive integer | Number of worker threads that group word matches into phrases. |
| `queue_size` | `16` | Positive integer | Maximum number of phrase packages buffered between the phrase scanner and the writer. The phrase scanner blocks when this limit is reached, applying back-pressure. |

### `[analysis]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `run_after_pipeline` | `false` | `true`, `false` | When true, automatically runs the result analyzer after the pipeline finishes. Writes per-length phrase files and `statistics.txt` into the run directory. When enabled, the timing output includes a separate `Analysis time` line in addition to `Pipeline time` and `Total time`. |
| `threads` | `1` | Positive integer | Number of worker threads used by the result analyzer. The `phrases` array in `results.json` is split into roughly equal byte-range chunks (one per thread) that are parsed and written in parallel, then merged into the final per-length phrase files and `statistics.txt`. |

\* Only one value is currently supported; the field is validated on startup.

## Dictionary

The following are the dictionaries which come prepackaged with Pi Poetry. Any dictionary can be used. The only requirements are that the dictionary file consists of text with words separated by a newline character. It's not necessary for the words to appear alphabetically.

`dictionaries/english.txt` is derived from the [SCOWL](http://wordlist.aspell.net/) (Spelling Checker Oriented Word Lists) large American English word list, distributed via the `wamerican-large` Debian package. SCOWL is made available under a permissive open-source licence — see [SCOWL copyright](http://wordlist.aspell.net/scowl-readme/) for details. Only lowercase words are retained.

`dictionaries/english_trimmed.txt` is a filtered version of `english.txt` with all 1- and 2-letter words removed, except for common ones: `a`, `i`, `am`, `an`, `as`, `at`, `aw`, `ax`, `be`, `by`, `do`, `ex`, `ha`, `hi`, `if`, `in`, `is`, `it`, `my`, `no`, `of`, `oh`, `on`, `or`, `ox`, `pi`, `so`, `to`, `uh`, `um`, `up`, `us`, `we`. Use this dictionary to reduce noise from obscure short words in results.

`dictionaries/google-10000-english.txt` is the list of the 10,000 most common English words, ordered by frequency, derived from the [Google Trillion Word Corpus](https://github.com/first20hours/google-10000-english). Because it contains only high-frequency everyday words, it produces cleaner, more readable results than the full SCOWL list while still covering a broad vocabulary.

`dictionaries/google-10000-english-trimmed.txt` is a filtered version of `google-10000-english.txt` with all 1- and 2-letter words removed, except for common ones: `a`, `i`, `am`, `an`, `as`, `at`, `aw`, `ax`, `be`, `by`, `do`, `ex`, `ha`, `hi`, `if`, `in`, `is`, `it`, `my`, `no`, `of`, `oh`, `ok`, `on`, `or`, `ox`, `pi`, `so`, `to`, `uh`, `um`, `up`, `us`, `we`. Frequency order is preserved. Use this dictionary to reduce noise from obscure short words in results.

## Project Structure

```
config/          Configuration files
data/            Place digit files here (not tracked by git)
dictionaries/    English word list
include/         Abstract interface headers and concrete class headers
src/             Implementation source files
tests/           Google Test unit and integration tests
```

## Utility Programs

### pi_download

Downloads digits of pi from the [pi.delivery](https://pi.delivery) API and writes them to a plain-text file in the `data/` directory, ready for use with the main pipeline. Alternatively, the main pipeline can download digits on-the-fly using `digit_source.type = "api"` (see above), without needing a pre-downloaded file.

**Compile:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4 --target pi_download
```

**Usage:**

```
./build/pi_download <num_digits> [--file <path>] [--output <path>]
./build/pi_download --help
```

| Argument | Required | Description |
|----------|----------|-------------|
| `num_digits` | Yes | Total number of pi digits the output file should contain. Supports values beyond 2 billion. |
| `--file <path>` | No | Append to an existing digit file. The file's size on disk determines the starting offset, so a previously interrupted run resumes automatically on the next invocation. |
| `--output <path>` | No | Path for a fresh download (no existing file). Defaults to `data/pi_<num_digits>.txt`. |
| `--help` | No | Print a usage summary and exit. |

Digits are fetched in chunks of 1,000 (the API maximum per request). Progress is printed to stdout roughly every 2% of total chunks (~50 lines for a full run). If any request fails, the program exits and — when using `--file` — the file retains all digits written so far; re-running resumes from that point automatically.

The output file contains raw decimal digits, one per byte, with no punctuation and no trailing newline (e.g. `31415926…`). File size equals digit count exactly.

**Examples:**

```bash
# Download 10,000 digits → data/pi_10000.txt
./build/pi_download 10000

# Download 500 digits to a custom path
./build/pi_download 500 --output data/small.txt

# Extend an existing file to 2 billion digits total
./build/pi_download 2000000000 --file data/pi_1p5e9.txt
```

In VSCode, use **Terminal > Run Task > Run: pi_download** — it will prompt for the digit count before running.

### gen_mapping

Generates a custom mapping file for use with `digit_mapper.type = "mapping-file"`. It reads a dictionary file, computes the frequency of each letter across all words, and produces a mapping where high-frequency letters are assigned proportionally more digit combos than low-frequency ones — every letter appears at least once. The combo-to-letter assignments are randomly shuffled so that a letter's slots are spread evenly across the digit range rather than clustered together.

**Compile:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4 --target gen_mapping
```

**Usage:**

```
./build/gen_mapping <dict_file> <digits> <output_path> [--seed N]
```

| Argument | Required | Description |
|----------|----------|-------------|
| `dict_file` | Yes | Path to the dictionary file to compute letter frequencies from. Only alphabetic characters are counted; digits, spaces, and punctuation are ignored. Case is folded to lowercase. |
| `digits` | Yes | Number of digits per character. Controls the total number of mapping slots: 10^digits (e.g. `2` → 100 slots, `3` → 1000 slots). Must be large enough to give every unique letter at least one slot; the program exits with an error otherwise. |
| `output_path` | Yes | Where to write the mapping file. Existing files are overwritten. |
| `--seed N` | No | Integer seed for the random shuffle. Using the same seed on the same input always produces identical output. Omit for a different mapping on each run. |

**Examples:**

```bash
# Generate a frequency-proportional 2-digit mapping from the English dictionary
./build/gen_mapping dictionaries/english.txt 2 config/mappings/english_freq.txt

# Same mapping every time (reproducible)
./build/gen_mapping dictionaries/english.txt 2 config/mappings/english_freq.txt --seed 42

# Use the generated mapping in the pipeline
# In config/default.toml:
#   [digit_mapper]
#   type = "mapping-file"
#   mapping_file = "config/mappings/english_freq.txt"
```

The output file is in the [mapping file format](#mapping-file-format-type--mapping-file) described above and can be used directly with `digit_mapper.type = "mapping-file"`.

### analyze_results

Analyzes a `results.json` file produced by the main pipeline. Can be run automatically after the pipeline by setting `run_after_pipeline = true` in the `[analysis]` config section, or manually as a standalone utility. Writes two kinds of output into the run directory:

- **Per-length phrase files** (`phrases_length_1.txt`, `phrases_length_2.txt`, …) — one file for each distinct phrase length found in the results. Each line lists the phrase's starting character offset followed by its words: `<offset>: word1 word2 … wordN`.
- **Statistics file** (`statistics.txt`) — phrase counts broken down by length, and the ten longest distinct words found across all phrases with their exact character offsets.

**Compile:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4 --target analyze_results
```

**Usage:**

```
./build/analyze_results <output_dir> [threads]
```

| Argument | Required | Description |
|----------|----------|-------------|
| `output_dir` | Yes | Path to a run directory that contains `results.json` (e.g. `outputs/run-20260508_143022`). Output files are written into the same directory. |
| `threads` | No | Number of worker threads to use (default: 1). The `phrases` array is split into roughly equal byte-range chunks, one per thread, processed in parallel and then merged. |

**Example:**

```bash
# Run the main pipeline to produce results
./build/pi_poetry --config config/default.toml

# Analyze the most recent run
./build/analyze_results outputs/run-20260508_143022

# Analyze using 8 threads
./build/analyze_results outputs/run-20260508_143022 8
```
