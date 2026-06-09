#include <gtest/gtest.h>
#include "gen_mapping/GenMapping.hpp"
#include "digit_mapper/MappingFileMapper.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace {

std::filesystem::path write_temp_gm(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() /
        ("pi_poetry_gm_test_" + std::to_string(
            std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".txt");
    std::ofstream f(path);
    f << content;
    return path;
}

}  // namespace

// ── Cycle 1: count_char_frequencies ─────────────────────────────────────────

TEST(CountCharFrequencies, EmptyStringReturnsEmptyMap) {
    EXPECT_TRUE(gen_mapping::count_char_frequencies("").empty());
}

TEST(CountCharFrequencies, SingleLowercaseLetter) {
    auto f = gen_mapping::count_char_frequencies("a");
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f.at('a'), 1u);
}

TEST(CountCharFrequencies, UppercaseFoldsToLowercase) {
    auto f = gen_mapping::count_char_frequencies("A");
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f.at('a'), 1u);
}

TEST(CountCharFrequencies, NonAlphaIgnored) {
    auto f = gen_mapping::count_char_frequencies("123 !@#\n\t");
    EXPECT_TRUE(f.empty());
}

TEST(CountCharFrequencies, MixedCase) {
    auto f = gen_mapping::count_char_frequencies("Hello World");
    EXPECT_EQ(f.at('h'), 1u);
    EXPECT_EQ(f.at('e'), 1u);
    EXPECT_EQ(f.at('l'), 3u);
    EXPECT_EQ(f.at('o'), 2u);
    EXPECT_EQ(f.at('w'), 1u);
    EXPECT_EQ(f.at('r'), 1u);
    EXPECT_EQ(f.at('d'), 1u);
    EXPECT_EQ(f.size(), 7u);
}

// ── Cycle 2: allocate_slots fail-fast ────────────────────────────────────────

TEST(AllocateSlots, TotalSlotsMatchesPowerOfTen) {
    std::map<char, std::size_t> one_char{{'a', 1}};
    EXPECT_EQ(gen_mapping::allocate_slots(one_char, 1).total_slots, 10u);
    EXPECT_EQ(gen_mapping::allocate_slots(one_char, 2).total_slots, 100u);
    EXPECT_EQ(gen_mapping::allocate_slots(one_char, 3).total_slots, 1000u);
}

TEST(AllocateSlots, ThrowsWhenTooManyChars) {
    // 11 unique chars, digits=1 → 10 slots, must throw
    std::map<char, std::size_t> eleven;
    for (char c = 'a'; c <= 'k'; ++c) eleven[c] = 1;
    EXPECT_THROW(gen_mapping::allocate_slots(eleven, 1), std::invalid_argument);
}

TEST(AllocateSlots, ExactlyAtCapacityNoThrow) {
    // 10 unique chars, digits=1 → exactly 10 slots, must not throw
    std::map<char, std::size_t> ten;
    for (char c = 'a'; c <= 'j'; ++c) ten[c] = 1;
    EXPECT_NO_THROW(gen_mapping::allocate_slots(ten, 1));
}

// ── Cycle 3: allocate_slots proportional ─────────────────────────────────────

TEST(AllocateSlots, SingleCharGetsAllSlots) {
    auto r = gen_mapping::allocate_slots({{'a', 42}}, 1);
    EXPECT_EQ(r.slots.at('a'), 10u);
    EXPECT_EQ(r.total_slots, 10u);
}

TEST(AllocateSlots, TwoEqualCharsDigits1) {
    auto r = gen_mapping::allocate_slots({{'a', 5}, {'b', 5}}, 1);
    EXPECT_EQ(r.slots.at('a'), 5u);
    EXPECT_EQ(r.slots.at('b'), 5u);
}

TEST(AllocateSlots, SumEqualsTotal) {
    auto r = gen_mapping::allocate_slots({{'a', 3}, {'b', 1}, {'c', 1}}, 1);
    std::size_t sum = 0;
    for (auto& [ch, n] : r.slots) sum += n;
    EXPECT_EQ(sum, r.total_slots);
}

TEST(AllocateSlots, AllCharsGetAtLeastOne) {
    // lopsided: 'z' is rare
    std::map<char, std::size_t> freq{{'a', 1000}, {'b', 1000}, {'z', 1}};
    auto r = gen_mapping::allocate_slots(freq, 2);
    for (auto& [ch, n] : r.slots) EXPECT_GE(n, 1u) << "char=" << ch;
}

// ── Cycle 4: assign_combos ───────────────────────────────────────────────────

TEST(AssignCombos, Digits1Generates10Combos) {
    auto r = gen_mapping::allocate_slots({{'a', 1}}, 1);
    auto v = gen_mapping::assign_combos(r, 1);
    ASSERT_EQ(v.size(), 10u);
    EXPECT_EQ(v.front().first, "0");
    EXPECT_EQ(v.back().first, "9");
    for (auto& p : v) EXPECT_EQ(p.first.size(), 1u);
}

TEST(AssignCombos, Digits2Generates100ZeroPadded) {
    auto r = gen_mapping::allocate_slots({{'a', 1}}, 2);
    auto v = gen_mapping::assign_combos(r, 2);
    ASSERT_EQ(v.size(), 100u);
    EXPECT_EQ(v.front().first, "00");
    EXPECT_EQ(v.back().first, "99");
    for (auto& p : v) EXPECT_EQ(p.first.size(), 2u);
}

TEST(AssignCombos, CharCountsMatchAllocation) {
    // a=5, b=3, c=2 for digits=1 (using hand-computed case)
    auto r = gen_mapping::allocate_slots({{'a', 3}, {'b', 1}, {'c', 1}}, 1);
    auto v = gen_mapping::assign_combos(r, 1);
    std::map<char, std::size_t> counts;
    for (auto& p : v) ++counts[p.second];
    for (auto& [ch, slots] : r.slots)
        EXPECT_EQ(counts[ch], slots) << "char=" << ch;
}

TEST(AssignCombos, TotalSizeEqualsTotal_Slots) {
    auto r = gen_mapping::allocate_slots({{'x', 5}, {'y', 3}}, 2);
    EXPECT_EQ(gen_mapping::assign_combos(r, 2).size(), r.total_slots);
}

// ── Cycle 5: shuffle_assignments ─────────────────────────────────────────────

TEST(ShuffleAssignments, SameSeedProducesSameResult) {
    auto r = gen_mapping::allocate_slots({{'a', 3}, {'b', 1}, {'c', 1}}, 1);
    auto v1 = gen_mapping::assign_combos(r, 1);
    auto v2 = v1;
    gen_mapping::shuffle_assignments(v1, 42u);
    gen_mapping::shuffle_assignments(v2, 42u);
    EXPECT_EQ(v1, v2);
}

TEST(ShuffleAssignments, DifferentSeedsProduceDifferentResults) {
    auto r = gen_mapping::allocate_slots({{'a', 1}, {'b', 1}}, 2);  // 100 slots, 2 chars
    auto v1 = gen_mapping::assign_combos(r, 2);
    auto v2 = v1;
    gen_mapping::shuffle_assignments(v1, 1u);
    gen_mapping::shuffle_assignments(v2, 2u);
    EXPECT_NE(v1, v2);
}

TEST(ShuffleAssignments, ShufflePreservesCharacterCounts) {
    auto r = gen_mapping::allocate_slots({{'a', 3}, {'b', 1}, {'c', 1}}, 1);
    auto v = gen_mapping::assign_combos(r, 1);
    std::map<char, std::size_t> before_chars, after_chars;
    for (auto& [combo, ch] : v) ++before_chars[ch];
    gen_mapping::shuffle_assignments(v, 99u);
    for (auto& [combo, ch] : v) ++after_chars[ch];
    EXPECT_EQ(before_chars, after_chars);
}

TEST(ShuffleAssignments, CombosRemainInSequentialOrder) {
    auto r = gen_mapping::allocate_slots({{'a', 3}, {'b', 1}, {'c', 1}}, 1);
    auto v = gen_mapping::assign_combos(r, 1);
    gen_mapping::shuffle_assignments(v, 42u);
    EXPECT_EQ(v.front().first, "0");
    EXPECT_EQ(v.back().first, "9");
    for (std::size_t i = 1; i < v.size(); ++i)
        EXPECT_LT(v[i-1].first, v[i].first);
}

TEST(ShuffleAssignments, NulloptSeedDoesNotThrow) {
    auto r = gen_mapping::allocate_slots({{'a', 1}}, 1);
    auto v = gen_mapping::assign_combos(r, 1);
    EXPECT_NO_THROW(gen_mapping::shuffle_assignments(v, std::nullopt));
}

// ── Cycle 6: write_mapping ───────────────────────────────────────────────────

TEST(WriteMapping, HeaderLinesPresent) {
    auto r = gen_mapping::allocate_slots({{'a', 1}}, 2);
    auto v = gen_mapping::assign_combos(r, 2);
    std::ostringstream oss;
    gen_mapping::write_mapping(v, 2, oss);
    std::string out = oss.str();
    EXPECT_NE(out.find("digits_per_char=2"), std::string::npos);
    EXPECT_NE(out.find("base=10"), std::string::npos);
}

TEST(WriteMapping, EntryCountMatchesTotal_Slots) {
    auto r = gen_mapping::allocate_slots({{'a', 1}}, 1);
    auto v = gen_mapping::assign_combos(r, 1);
    std::ostringstream oss;
    gen_mapping::write_mapping(v, 1, oss);
    std::istringstream iss(oss.str());
    std::string line;
    int entry_count = 0;
    while (std::getline(iss, line)) {
        if (!line.empty() && line[0] != '#' &&
            line.find('=') == std::string::npos)
            ++entry_count;
    }
    EXPECT_EQ(entry_count, 10);
}

TEST(WriteMapping, EntryFormatIsComboSpaceChar) {
    auto r = gen_mapping::allocate_slots({{'a', 1}, {'b', 1}}, 2);
    auto v = gen_mapping::assign_combos(r, 2);
    std::ostringstream oss;
    gen_mapping::write_mapping(v, 2, oss);
    std::istringstream iss(oss.str());
    std::string line;
    std::regex entry_re(R"([0-9]{2} [a-z])");
    int entry_count = 0;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#' || line.find('=') != std::string::npos)
            continue;
        EXPECT_TRUE(std::regex_match(line, entry_re)) << "bad line: " << line;
        ++entry_count;
    }
    EXPECT_EQ(entry_count, 100);
}

TEST(WriteMapping, RoundTripWithMappingFileMapper) {
    auto r = gen_mapping::allocate_slots({{'a', 1}, {'b', 1}}, 2);
    auto v = gen_mapping::assign_combos(r, 2);
    std::ostringstream oss;
    gen_mapping::write_mapping(v, 2, oss);
    auto tmp = write_temp_gm(oss.str());
    MappingFileMapper m(tmp);
    std::filesystem::remove(tmp);
    EXPECT_EQ(m.digits_per_char(), 2);
    EXPECT_EQ(m.required_base(), 10);
    EXPECT_EQ(m.alphabet_size(), 2u);
}

// ── Cycle 7: file I/O helpers ─────────────────────────────────────────────────

TEST(ReadDictFile, ThrowsOnMissingFile) {
    EXPECT_THROW(gen_mapping::read_dict_file("/nonexistent/path/no_file.txt"),
                 std::runtime_error);
}

TEST(ReadDictFile, ReturnsFullContent) {
    auto path = write_temp_gm("hello\nworld\n");
    std::string content = gen_mapping::read_dict_file(path);
    std::filesystem::remove(path);
    EXPECT_EQ(content, "hello\nworld\n");
}

TEST(WriteMappingFile, RoundTripWithMappingFileMapper) {
    auto r = gen_mapping::allocate_slots({{'x', 1}, {'y', 1}}, 2);
    auto v = gen_mapping::assign_combos(r, 2);
    auto tmp = std::filesystem::temp_directory_path() / "pi_poetry_gm_write_test.txt";
    gen_mapping::write_mapping_file(v, 2, tmp);
    MappingFileMapper m(tmp);
    std::filesystem::remove(tmp);
    EXPECT_EQ(m.digits_per_char(), 2);
    EXPECT_EQ(m.required_base(), 10);
    EXPECT_EQ(m.alphabet_size(), 2u);
}

// ── Cycle 8: end-to-end integration ──────────────────────────────────────────

static std::filesystem::path run_gen_mapping(const std::string& dict_text, int digits,
                                              std::optional<uint64_t> seed = std::nullopt) {
    auto tmp = std::filesystem::temp_directory_path() /
        ("pi_poetry_gm_e2e_" + std::to_string(
            std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".txt");
    auto freqs = gen_mapping::count_char_frequencies(dict_text);
    auto alloc = gen_mapping::allocate_slots(freqs, digits);
    auto v = gen_mapping::assign_combos(alloc, digits);
    gen_mapping::shuffle_assignments(v, seed);
    gen_mapping::write_mapping_file(v, digits, tmp);
    return tmp;
}

TEST(GenMappingIntegration, EnglishDictDigits2ProducesValidMapping) {
    std::string dict = gen_mapping::read_dict_file(DICT_PATH);
    auto tmp = run_gen_mapping(dict, 2, 42u);
    MappingFileMapper m(tmp);
    std::filesystem::remove(tmp);
    EXPECT_EQ(m.digits_per_char(), 2);
    EXPECT_EQ(m.required_base(), 10);
    EXPECT_EQ(m.alphabet_size(), 26u);
}

TEST(GenMappingIntegration, SeedReproducibility) {
    std::string dict = gen_mapping::read_dict_file(DICT_PATH);
    auto tmp1 = run_gen_mapping(dict, 2, 77u);
    auto tmp2 = run_gen_mapping(dict, 2, 77u);
    // Read both files and compare byte-for-byte
    std::ifstream f1(tmp1), f2(tmp2);
    std::string s1{std::istreambuf_iterator<char>(f1), {}};
    std::string s2{std::istreambuf_iterator<char>(f2), {}};
    std::filesystem::remove(tmp1);
    std::filesystem::remove(tmp2);
    EXPECT_EQ(s1, s2);
}

TEST(GenMappingIntegration, TooManyCharsThrows) {
    // 11 unique chars, 1 digit → only 10 slots
    std::map<char, std::size_t> eleven;
    for (char c = 'a'; c <= 'k'; ++c) eleven[c] = 1;
    EXPECT_THROW(gen_mapping::allocate_slots(eleven, 1), std::invalid_argument);
}

TEST(AllocateSlots, LargestRemainderHandComputedCase) {
    // {a:3, b:1, c:1}, digits=1, total=10
    // guaranteed 1 each → remaining=7
    // proportional of 7: a=7*3/5=4.2, b=7*1/5=1.4, c=7*1/5=1.4
    // floors: a=4, b=1, c=1 → sum=6, leftover=1
    // remainders: a=0.2, b=0.4, c=0.4 → tie b,c; alphabetical → b wins
    // result: a=5, b=3, c=2
    auto r = gen_mapping::allocate_slots({{'a', 3}, {'b', 1}, {'c', 1}}, 1);
    EXPECT_EQ(r.slots.at('a'), 5u);
    EXPECT_EQ(r.slots.at('b'), 3u);
    EXPECT_EQ(r.slots.at('c'), 2u);
}
