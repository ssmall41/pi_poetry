#include "result_analyzer/ResultAnalyzer.hpp"
#include <chrono>
#include <iomanip>
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
    auto start = std::chrono::steady_clock::now();
    try {
        result_analyzer::analyze(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Total time: " << std::fixed << std::setprecision(3) << elapsed << "s\n";
        return 1;
    }
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Total time: " << std::fixed << std::setprecision(3) << elapsed << "s\n";
    return 0;
}
