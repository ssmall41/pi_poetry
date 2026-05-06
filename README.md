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

Results are written to `results.txt` and `results.json` (configurable in `config/default.toml`).

## Running Tests

```bash
ctest --test-dir build --output-on-failure
```

## VSCode

Open the folder in VSCode. Use **Ctrl+Shift+B** to build, the Testing panel (flask icon) to run tests, and **F5** to debug.

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
