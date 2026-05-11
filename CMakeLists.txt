cmake_minimum_required(VERSION 3.28)
project(pi_poetry VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
FetchContent_MakeAvailable(nlohmann_json tomlplusplus googletest cpp-httplib)

# ── Stage libraries ───────────────────────────────────────────────────────────
add_library(digit_source_lib STATIC src/digit_source/FileDigitSource.cpp)
target_include_directories(digit_source_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(digit_source_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(digit_mapper_lib STATIC src/digit_mapper/TwoDigitBlockMapper.cpp)
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
    digit_source_lib digit_mapper_lib word_finder_lib phrase_scanner_lib)
target_compile_options(pipeline_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(result_analyzer_lib STATIC src/result_analyzer/ResultAnalyzer.cpp)
target_include_directories(result_analyzer_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(result_analyzer_lib PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(result_analyzer_lib PRIVATE -Wall -Wextra -Wpedantic)

add_library(config_validator_lib STATIC src/config_validator.cpp)
target_include_directories(config_validator_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(config_validator_lib PUBLIC tomlplusplus::tomlplusplus)
target_compile_options(config_validator_lib PRIVATE -Wall -Wextra -Wpedantic)

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
)
target_link_libraries(pi_poetry_tests PRIVATE
    digit_source_lib digit_mapper_lib word_finder_lib phrase_scanner_lib
    pipeline_lib
    config_validator_lib
    result_analyzer_lib
    GTest::gtest_main
)
target_compile_definitions(pi_poetry_tests PRIVATE
    PI_POETRY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    DICT_PATH="${CMAKE_SOURCE_DIR}/dictionaries/english.txt"
)
target_compile_options(pi_poetry_tests PRIVATE -Wall -Wextra -Wpedantic)

gtest_discover_tests(pi_poetry_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
