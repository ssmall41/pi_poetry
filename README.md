# Pi Poetry

Pi Poetry searches for natural-language words and phrases hidden in the decimal expansion of pi. It maps digit pairs to letters and scans the resulting character stream with an Aho-Corasick automaton, then groups found words into phrases.

## Building

**Requirements:** GCC 13+, CMake 3.28+. Dependencies (nlohmann/json, toml++, Google Test) are downloaded automatically via CMake FetchContent.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
```

## Running

Supply a plain-text file of pi digits (one ASCII digit per byte) at `data/pi_digits.txt`, or adjust `config/default.toml` to point at your file. Then:

```bash
./build/pi_poetry --config config/default.toml
```

Each run creates a timestamped subfolder (e.g. `outputs/run-20260506_143022/`) containing `results.json`. If `write_letter_sequence = true` in the `[digit_mapper]` config, `letter_sequence.txt` is also written. The output directory is configurable in `config/default.toml`.

## Running Tests

```bash
ctest --test-dir build --output-on-failure
```

## VSCode

Open the folder in VSCode. Use **Ctrl+Shift+B** to build, the Testing panel (flask icon) to run tests, and **F5** to debug.

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
| `type` * | `"file"` | `"file"` | Digit source implementation to use. |
| `path` | `"data/pi_2000.txt"` | Any file path | Plain-text file of pi digits, one ASCII digit per byte. |
| `threads` | `1` | Positive integer | Number of worker threads that read digit chunks from the source and push them into the pipeline. |
| `chunk_size` | `131072` | Positive integer | Number of digits each worker reads per work package. Values that are not a multiple of `digits_per_char` (2 for the default mapper) are silently rounded up. Larger chunks reduce coordination overhead; smaller chunks increase parallelism granularity. |
| `queue_size` | `16` | Positive integer | Maximum number of digit packages buffered between the digit source and the digit mapper. The digit source blocks when this limit is reached, applying back-pressure. |

### `[digit_mapper]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"two-digit-block"` | `"two-digit-block"` | Mapper implementation to use. |
| `alphabet` * | `"alpha-lower"` | `"alpha-lower"` | Character set to map digit pairs into. |
| `base` * | `10` | `10` | Numeric base of the digit stream. |
| `threads` | `1` | Positive integer | Number of worker threads that convert digit packages to letter packages. |
| `write_letter_sequence` | `false` | `true`, `false` | When true, writes the mapped letter sequence to `letter_sequence.txt` in the run directory. |
| `queue_size` | `16` | Positive integer | Maximum number of letter packages buffered between the digit mapper and the word finder. The digit mapper blocks when this limit is reached, applying back-pressure. |

### `[word_finder]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"aho-corasick-cpu"` | `"aho-corasick-cpu"` | Word-finder implementation to use. |
| `dictionary` | `"dictionaries/english.txt"` | Any file path | Path to the word list, one word per line. |
| `overlap_policy` | `"earliest-then-longest"` | `"earliest-then-longest"`, `"all-combos"` | How to resolve overlapping word matches. `"earliest-then-longest"` greedily picks one non-overlapping sequence: the match starting soonest, with ties broken by longest word. `"all-combos"` enumerates every possible consecutive chain of non-overlapping words; use this to explore all valid readings. |
| `min_word_length` | `3` | Positive integer | Words shorter than this are ignored when loading the dictionary. |
| `threads` | `1` | Positive integer | Number of worker threads that scan letter packages for dictionary words. |
| `queue_size` | `16` | Positive integer | Maximum number of combo packages buffered between the word finder and the phrase scanner. The word finder blocks when this limit is reached, applying back-pressure. |

### `[phrase_scanner]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"human-review"` | `"human-review"` | Phrase-scanner implementation to use. |
| `threads` | `1` | Positive integer | Number of worker threads that group word matches into phrases. |
| `queue_size` | `16` | Positive integer | Maximum number of phrase packages buffered between the phrase scanner and the writer. The phrase scanner blocks when this limit is reached, applying back-pressure. |

### `[analysis]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `run_after_pipeline` | `false` | `true`, `false` | When true, automatically runs the result analyzer after the pipeline finishes. Writes per-length phrase files and `statistics.txt` into the run directory. When enabled, the timing output includes a separate `Analysis time` line in addition to `Pipeline time` and `Total time`. |

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

Downloads digits of pi from the [pi.delivery](https://pi.delivery) API and writes them to a plain-text file in the `data/` directory, ready for use with the main pipeline.

**Compile:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target pi_download
```

**Usage:**

```
./build/pi_download <num_digits> [--output <path>]
```

| Argument | Required | Description |
|----------|----------|-------------|
| `num_digits` | Yes | Total number of pi digits to download. |
| `--output <path>` | No | Path to write the output file. Defaults to `data/pi_<num_digits>.txt`. |

Digits are fetched in chunks of 1,000 (the API maximum per request) and progress is printed to stdout. If any request fails the program exits with an error and no file is written. The output is raw decimal digits with no punctuation (e.g. `31415926...`), one digit per byte, with a trailing newline — the format expected by the main pipeline.

**Examples:**

```bash
# Download 10,000 digits → data/pi_10000.txt
./build/pi_download 10000

# Download 500 digits to a custom path
./build/pi_download 500 --output data/small.txt
```

In VSCode, use **Terminal > Run Task > Run: pi_download** — it will prompt for the digit count before running.

### analyze_results

Analyzes a `results.json` file produced by the main pipeline. Can be run automatically after the pipeline by setting `run_after_pipeline = true` in the `[analysis]` config section, or manually as a standalone utility. Writes two kinds of output into the run directory:

- **Per-length phrase files** (`phrases_length_1.txt`, `phrases_length_2.txt`, …) — one file for each distinct phrase length found in the results. Each line lists the phrase's starting character offset followed by its words: `<offset>: word1 word2 … wordN`.
- **Statistics file** (`statistics.txt`) — phrase counts broken down by length, and the ten longest distinct words found across all phrases with their exact character offsets.

**Compile:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target analyze_results
```

**Usage:**

```
./build/analyze_results <output_dir>
```

| Argument | Required | Description |
|----------|----------|-------------|
| `output_dir` | Yes | Path to a run directory that contains `results.json` (e.g. `outputs/run-20260508_143022`). Output files are written into the same directory. |

**Example:**

```bash
# Run the main pipeline to produce results
./build/pi_poetry --config config/default.toml

# Analyze the most recent run
./build/analyze_results outputs/run-20260508_143022
```

## Technical Design

See [pi_poetry_tdd_v3.md](pi_poetry_tdd_v3.md) for the full technical design document.
