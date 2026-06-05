#include <httplib.h>
#include <nlohmann/json.hpp>
#include "download_pi_utils.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

static constexpr int kChunkSize = 1000;

struct Args {
    int64_t num_digits;
    std::string output_path;
    std::string file_path;
};

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " <num_digits> [--file <path>] [--output <path>]\n";
}

static void print_help(const char* prog) {
    std::cout <<
        "Usage: " << prog << " <num_digits> [--file <path>] [--output <path>]\n"
        "\n"
        "Downloads digits of pi from api.pi.delivery and writes them to a file.\n"
        "Digits are written one per byte with no newline or punctuation.\n"
        "\n"
        "Arguments:\n"
        "  <num_digits>      Total number of pi digits the file should contain.\n"
        "                    Must be a positive integer (supports values beyond 2 billion).\n"
        "\n"
        "Options:\n"
        "  --file <path>     Append to an existing digit file. The file size is used\n"
        "                    as the starting offset, so interrupted downloads resume\n"
        "                    automatically on the next run. Cannot be combined with\n"
        "                    --output.\n"
        "  --output <path>   Write a fresh download to this path. Defaults to\n"
        "                    data/pi_<num_digits>.txt when --file is not given.\n"
        "  --help            Show this help message and exit.\n"
        "\n"
        "Progress is printed to stdout roughly every 2% of total chunks.\n"
        "\n"
        "Examples:\n"
        "  " << prog << " 10000\n"
        "      Download 10,000 digits to data/pi_10000.txt\n"
        "\n"
        "  " << prog << " 500 --output data/small.txt\n"
        "      Download 500 digits to a custom path\n"
        "\n"
        "  " << prog << " 2000000000 --file data/pi_1p5e9.txt\n"
        "      Extend an existing file to 2 billion digits total\n";
}

static Args parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        std::exit(1);
    }

    if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        print_help(argv[0]);
        std::exit(0);
    }

    Args args;
    try {
        args.num_digits = std::stoll(argv[1]);
    } catch (const std::exception&) {
        std::cerr << "Error: invalid number of digits: " << argv[1] << "\n";
        std::exit(1);
    }
    if (args.num_digits <= 0) {
        std::cerr << "Error: num_digits must be positive\n";
        std::exit(1);
    }

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            args.output_path = argv[++i];
        } else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            args.file_path = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << argv[i] << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    if (args.output_path.empty()) {
        args.output_path = "data/pi_" + std::to_string(args.num_digits) + ".txt";
    }

    return args;
}

// Streams downloaded digits directly to out, starting at start_offset.
// Returns true on success.
static bool download_pi(int64_t num_digits, int64_t start_offset,
                        std::ofstream& out) {
    httplib::SSLClient client("api.pi.delivery");
    client.set_connection_timeout(30);
    client.set_read_timeout(30);
    client.set_keep_alive(true);

    int64_t total_chunks = (num_digits + kChunkSize - 1) / kChunkSize;
    int64_t print_every = std::max(int64_t{1}, total_chunks / 50);

    int64_t downloaded = 0;
    int64_t chunks_done = 0;
    while (downloaded < num_digits) {
        int64_t chunk = std::min(static_cast<int64_t>(kChunkSize),
                                 num_digits - downloaded);
        std::string path = "/v1/pi?start=" + std::to_string(start_offset + downloaded) +
                           "&numberOfDigits=" + std::to_string(chunk);

        auto res = client.Get(path);
        if (!res) {
            std::cerr << "Error: HTTP request failed at start="
                      << (start_offset + downloaded) << "\n";
            return false;
        }
        if (res->status != 200) {
            std::cerr << "Error: HTTP status " << res->status
                      << " at start=" << (start_offset + downloaded) << "\n";
            return false;
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(res->body);
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Error: failed to parse JSON response: " << e.what() << "\n";
            return false;
        }

        if (!j.contains("content") || !j["content"].is_string()) {
            std::cerr << "Error: JSON response missing 'content' field\n";
            return false;
        }

        out << j["content"].get<std::string>();
        if (!out) {
            std::cerr << "Error: failed to write to output file\n";
            return false;
        }

        downloaded += chunk;
        ++chunks_done;
        if (chunks_done % print_every == 0 || downloaded == num_digits) {
            std::cout << "Downloaded " << (start_offset + downloaded) << "/"
                      << (start_offset + num_digits) << " digits\n";
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);
    auto start = std::chrono::steady_clock::now();

    int64_t start_offset = 0;
    std::string target_path;

    if (!args.file_path.empty()) {
        auto existing = get_existing_digit_count(args.file_path);
        if (!existing.has_value()) {
            std::cerr << "Error: failed to read or validate file: "
                      << args.file_path << "\n";
            return 1;
        }

        int64_t existing_count = *existing;
        if (existing_count >= args.num_digits) {
            std::cerr << "Error: file already contains " << existing_count
                      << " digits, which meets/exceeds requested total of "
                      << args.num_digits << ".\n";
            return 1;
        }

        start_offset = existing_count;
        target_path = args.file_path;
    } else {
        target_path = args.output_path;
    }

    int64_t additional = args.num_digits - start_offset;

    auto open_flags = args.file_path.empty()
        ? std::ios::out
        : std::ios::out | std::ios::app;
    std::ofstream out(target_path, open_flags);
    if (!out) {
        std::cerr << "Error: failed to open output file: " << target_path << "\n";
        return 1;
    }

    if (!download_pi(additional, start_offset, out)) {
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "Total time: " << std::fixed << std::setprecision(3) << elapsed << "s\n";
        return 1;
    }

    out.flush();
    std::cout << "Wrote " << additional << " digits to " << target_path << "\n";
    auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "Total time: " << std::fixed << std::setprecision(3) << elapsed << "s\n";
    return 0;
}
