#include "gen_mapping/GenMapping.hpp"
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

struct Args {
    std::filesystem::path dict_file;
    int digits;
    std::filesystem::path output_path;
    std::optional<uint64_t> seed;
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <dict_file> <digits> <output_path> [--seed N]\n"
              << "  dict_file   : path to dictionary file\n"
              << "  digits      : number of digits per character (determines 10^digits slots)\n"
              << "  output_path : where to write the mapping file\n"
              << "  --seed N    : optional seed for reproducible output\n";
}

Args parse_args(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        std::exit(1);
    }

    Args args;
    args.dict_file = argv[1];
    args.digits = std::stoi(argv[2]);
    if (args.digits < 1)
        throw std::invalid_argument("digits must be >= 1");
    args.output_path = argv[3];

    for (int i = 4; i < argc; ++i) {
        std::string flag = argv[i];
        if (flag == "--seed" && i + 1 < argc) {
            args.seed = static_cast<uint64_t>(std::stoull(argv[++i]));
        } else {
            throw std::invalid_argument("unknown argument: " + flag);
        }
    }
    return args;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        auto args = parse_args(argc, argv);
        auto text = gen_mapping::read_dict_file(args.dict_file);
        auto freqs = gen_mapping::count_char_frequencies(text);
        auto alloc = gen_mapping::allocate_slots(freqs, args.digits);
        auto assignments = gen_mapping::assign_combos(alloc, args.digits);
        gen_mapping::shuffle_assignments(assignments, args.seed);
        gen_mapping::write_mapping_file(assignments, args.digits, args.output_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
