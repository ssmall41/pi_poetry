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

Each run creates a timestamped subfolder (e.g. `outputs/run-20260506_143022/`) containing `results.txt`, `results.json`, and `debug_letters.txt`. The output directory is configurable in `config/default.toml`.

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
| `mode` * | `"serial"` | `"serial"` | Controls how pipeline stages are scheduled. |

### `[digit_source]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"file"` | `"file"` | Digit source implementation to use. |
| `path` | `"data/pi_2000.txt"` | Any file path | Plain-text file of pi digits, one ASCII digit per byte. |
| `threads` * | `1` | Positive integer | Worker threads for the digit source. |

### `[digit_mapper]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"two-digit-block"` | `"two-digit-block"` | Mapper implementation to use. |
| `alphabet` * | `"alpha-lower"` | `"alpha-lower"` | Character set to map digit pairs into. |
| `base` * | `10` | `10` | Numeric base of the digit stream. |
| `threads` * | `1` | Positive integer | Worker threads for the mapper. |

### `[word_finder]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"aho-corasick-cpu"` | `"aho-corasick-cpu"` | Word-finder implementation to use. |
| `dictionary` | `"dictionaries/english.txt"` | Any file path | Path to the word list, one word per line. |
| `overlap_policy` * | `"earliest-then-longest"` | `"earliest-then-longest"` | How to resolve overlapping word matches. `"earliest-then-longest"` picks the match starting soonest; ties broken by longest word. |
| `min_word_length` | `3` | Positive integer | Words shorter than this are ignored. |
| `threads` * | `1` | Positive integer | Worker threads for the word finder. |

### `[phrase_scanner]`

| Field | Default | Valid Values | Description |
|-------|---------|--------------|-------------|
| `type` * | `"human-review"` | `"human-review"` | Phrase-scanner implementation to use. |
| `mode` * | `"gap-tolerant"` | `"gap-tolerant"` | How gaps between consecutive words are treated. |
| `max_gap` | `5` | Non-negative integer | Maximum number of unmapped characters allowed between two consecutive words for them to be grouped into the same phrase. |
| `threads` * | `1` | Positive integer | Worker threads for the phrase scanner. |

\* Reserved — parsed but not yet used.

## Dictionary

`dictionaries/english.txt` is derived from the [SCOWL](http://wordlist.aspell.net/) (Spelling Checker Oriented Word Lists) large American English word list, distributed via the `wamerican-large` Debian package. SCOWL is made available under a permissive open-source licence — see [SCOWL copyright](http://wordlist.aspell.net/scowl-readme/) for details. Only lowercase words are retained.

## Project Structure

```
config/          Configuration files
data/            Place digit files here (not tracked by git)
dictionaries/    English word list
include/         Abstract interface headers and concrete class headers
src/             Implementation source files
tests/           Google Test unit and integration tests
```

## Technical Design

See [pi_poetry_tdd_v3.md](pi_poetry_tdd_v3.md) for the full technical design document.
