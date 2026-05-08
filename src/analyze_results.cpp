#include "result_analyzer/ResultAnalyzer.hpp"
#include <iostream>
#include <stdexcept>

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <output_dir>\n";
    std::cerr << "  Reads <output_dir>/results.json and writes per-length phrase\n";
    std::cerr << "  files and statistics.txt into the same directory.\n";
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }
    try {
        result_analyzer::analyze(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
