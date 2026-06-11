cmake_minimum_required(VERSION 3.28)
project(pi_poetry VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compile cache ─────────────────────────────────────────────────────────────
# If ccache is installed, route all compiles through it. Unchanged translation
# units are then served from cache on rebuilds/branch-switches/CI with no
# recompile (and no memory cost). Header-only deps (json/toml++/httplib) are
# the same headers regardless of source, so caching the resulting objects is
# the main lever for them, alongside the precompiled headers configured below.
# For PCH compatibility, set CCACHE_SLOPPINESS=pch_defines,time_macros in your
# environment (see README).
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_Declare(
    tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
FetchContent_Declare(
    cpp-httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.43.3.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# We link only GTest::gtest_main, never gmock. Skip building gmock/gmock_main
# (template-heavy TUs) and the gtest install rules — pure clean-build time saved.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# Disable httplib's non-blocking getaddrinfo: it spawns a std::thread per DNS
# lookup, which creates one thread per HTTP request when keep-alive is not yet
# established. Blocking getaddrinfo is fine here — DNS is OS-cached after the
# first lookup per thread.
set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
# Compile cpp-httplib once into a static library (a bundled Python script splits
# its single header into a .h + .cc) instead of inlining its ~10k-line
# implementation into every TU that includes <httplib.h>. Cuts both clean-build
# time and per-TU compile memory for the digit-source/download targets.
set(HTTPLIB_COMPILE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json tomlplusplus googletest cpp-httplib)

# Strip debug info from third-party dependency objects: we never step into
# httplib/gtest sources, and at -O0 -g their big single TUs (httplib.cc ~25s,
# gtest-all ~12s) are among the longest serial poles in a Debug clean build.
# -g0 trims ~10s off that critical path; Release/Perf already build them
# without -g, so this only affects Debug. (Our own targets keep full -g.)
foreach(_dep_target httplib gtest gtest_main)
    if(TARGET ${_dep_target})
        target_compile_options(${_dep_target} PRIVATE -g0)
    endif()
endforeach()

find_package(Threads REQUIRED)

# ── Stage libraries ───────────────────────────────────────────────────────────
add_library(digit_source_lib STATIC
    src/digit_source/FileDigitSource.cpp
    src/digit_source/ApiDigitSource.cpp
)
target_include_directories(digit_source_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(digit_source_lib PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads)
target_compile_definitions(digit_source_lib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_compile_options(digit_source_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(digit_mapper_lib STATIC
    src/digit_mapper/TwoDigitBlockMapper.cpp
    src/digit_mapper/MappingFileMapper.cpp
)
target_include_directories(digit_mapper_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(digit_mapper_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(word_finder_lib STATIC src/word_finder/AhoCorasickCPU.cpp)
target_include_directories(word_finder_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(word_finder_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(phrase_scanner_lib STATIC src/phrase_scanner/HumanReviewScanner.cpp)
target_include_directories(phrase_scanner_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(phrase_scanner_lib PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(phrase_scanner_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(pipeline_lib STATIC src/pipeline/Pipeline.cpp)
target_include_directories(pipeline_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(pipeline_lib PUBLIC
    digit_source_lib digit_mapper_lib word_finder_lib phrase_scanner_lib
    Threads::Threads)
target_compile_options(pipeline_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(result_analyzer_lib STATIC src/result_analyzer/ResultAnalyzer.cpp)
target_include_directories(result_analyzer_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(result_analyzer_lib PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(result_analyzer_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(config_validator_lib STATIC src/config_validator.cpp)
target_include_directories(config_validator_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(config_validator_lib PUBLIC tomlplusplus::tomlplusplus)
target_compile_options(config_validator_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(gen_mapping_lib STATIC src/gen_mapping/GenMapping.cpp)
target_include_directories(gen_mapping_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(gen_mapping_lib PRIVATE -Wall -Wextra -Wpedantic)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(pi_poetry src/main.cpp)
target_link_libraries(pi_poetry PRIVATE pipeline_lib config_validator_lib result_analyzer_lib tomlplusplus::tomlplusplus)
target_compile_definitions(pi_poetry PRIVATE
    PI_POETRY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(pi_poetry PRIVATE -Wall -Wextra -Wpedantic)

# ── Download utility ──────────────────────────────────────────────────────────
add_executable(pi_download src/download_pi.cpp)
target_compile_definitions(pi_download PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_include_directories(pi_download PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(pi_download PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_compile_options(pi_download PRIVATE -Wall -Wextra -Wpedantic)

# ── Analyze results utility ───────────────────────────────────────────────────
add_executable(analyze_results src/analyze_results.cpp)
target_link_libraries(analyze_results PRIVATE result_analyzer_lib)
target_compile_options(analyze_results PRIVATE -Wall -Wextra -Wpedantic)

# ── Gen-mapping utility ───────────────────────────────────────────────────────
add_executable(gen_mapping src/gen_mapping.cpp)
target_link_libraries(gen_mapping PRIVATE gen_mapping_lib)
target_compile_options(gen_mapping PRIVATE -Wall -Wextra -Wpedantic)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
include(GoogleTest)

add_executable(pi_poetry_tests
    tests/test_digit_source.cpp
    tests/test_digit_mapper.cpp
    tests/test_word_finder.cpp
    tests/test_phrase_scanner.cpp
    tests/test_pipeline.cpp
    tests/test_integration.cpp
    tests/test_config_validator.cpp
    tests/test_analyze_results.cpp
    tests/test_download_pi.cpp
    tests/test_bounded_queue.cpp
    tests/test_parallel_pipeline.cpp
    tests/test_reorder_buffer.cpp
    tests/test_api_digit_source.cpp
    tests/test_sentinel.cpp
    tests/test_mapping_file_mapper.cpp
    tests/test_gen_mapping.cpp
)
target_link_libraries(pi_poetry_tests PRIVATE
    digit_source_lib digit_mapper_lib word_finder_lib phrase_scanner_lib
    pipeline_lib
    config_validator_lib
    result_analyzer_lib
    gen_mapping_lib
    GTest::gtest_main
)
target_compile_definitions(pi_poetry_tests PRIVATE
    PI_POETRY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    DICT_PATH="${CMAKE_SOURCE_DIR}/dictionaries/english.txt"
)
target_compile_options(pi_poetry_tests PRIVATE -Wall -Wextra -Wpedantic)

# The test binary is 16 TUs, all including <gtest/gtest.h> and most including
# the ~25k-line <nlohmann/json.hpp>. Precompile that shared front-matter once
# for the target instead of re-parsing it per TU. The few light test TUs that
# don't use json pay only a cheap PCH load, not a full parse. This is the
# single biggest parse-time win in the build (multi-TU reuse is what makes PCH
# pay off, which is why the single-TU libraries above don't use it).
target_precompile_headers(pi_poetry_tests PRIVATE
    <gtest/gtest.h>
    <nlohmann/json.hpp>
    <string>
    <vector>
    <map>
    <filesystem>)

gtest_discover_tests(pi_poetry_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
