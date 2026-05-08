#include <httplib.h>
#include <nlohmann/json.hpp>
#include "download_pi_utils.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

static constexpr int kChunkSize = 1000;

struct Args {
    int num_digits;
    std::string output_path;
    std::string input_path;
};

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " <num_digits> [--input <path>] [--output <path>]\n";
}

static Args parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        std::exit(1);
    }

    Args args;
    try {
        args.num_digits = std::stoi(argv[1]);
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
        } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            args.input_path = argv[++i];
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

// Returns accumulated digit string starting at start_offset, or empty on error.
static std::string download_pi(int num_digits, int start_offset) {
    httplib::SSLClient client("api.pi.delivery");
    client.set_connection_timeout(30);
    client.set_read_timeout(30);

    std::string digits;
    digits.reserve(num_digits);

    int downloaded = 0;
    while (downloaded < num_digits) {
        int chunk = std::min(kChunkSize, num_digits - downloaded);
        std::string path = "/v1/pi?start=" + std::to_string(start_offset + downloaded) +
                           "&numberOfDigits=" + std::to_string(chunk);

        auto res = client.Get(path);
        if (!res) {
            std::cerr << "Error: HTTP request failed at start="
                      << (start_offset + downloaded) << "\n";
            return {};
        }
        if (res->status != 200) {
            std::cerr << "Error: HTTP status " << res->status
                      << " at start=" << (start_offset + downloaded) << "\n";
            return {};
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(res->body);
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Error: failed to parse JSON response: " << e.what() << "\n";
            return {};
        }

        if (!j.contains("content") || !j["content"].is_string()) {
            std::cerr << "Error: JSON response missing 'content' field\n";
            return {};
        }

        digits += j["content"].get<std::string>();
        downloaded += chunk;
        std::cout << "Downloaded " << (start_offset + downloaded) << "/"
                  << (start_offset + num_digits) << " digits\n";
    }

    return digits;
}

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);

    std::string output;

    if (!args.input_path.empty()) {
        auto existing = read_and_validate_input_file(args.input_path);
        if (!existing.has_value()) {
            std::cerr << "Error: failed to read or validate input file: "
                      << args.input_path << "\n";
            return 1;
        }

        int existing_count = static_cast<int>(existing->size());
        if (existing_count >= args.num_digits) {
            std::cerr << "Error: input file already contains " << existing_count
                      << " digits, which meets/exceeds requested total of "
                      << args.num_digits << ".\n";
            return 1;
        }

        int additional = args.num_digits - existing_count;
        std::string new_digits = download_pi(additional, existing_count);
        if (new_digits.empty()) {
            return 1;
        }
        output = std::move(*existing) + new_digits;
    } else {
        output = download_pi(args.num_digits, 0);
        if (output.empty()) {
            return 1;
        }
    }

    std::ofstream out(args.output_path);
    if (!out) {
        std::cerr << "Error: failed to open output file: " << args.output_path << "\n";
        return 1;
    }
    out << output << "\n";
    if (!out) {
        std::cerr << "Error: failed to write output file: " << args.output_path << "\n";
        return 1;
    }

    std::cout << "Wrote " << output.size() << " digits to " << args.output_path << "\n";
    return 0;
}
