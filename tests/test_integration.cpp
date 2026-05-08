#include <gtest/gtest.h>
#include "digit_mapper/TwoDigitBlockMapper.hpp"
#include "word_finder/AhoCorasickCPU.hpp"
#include "phrase_scanner/HumanReviewScanner.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<uint8_t> parse_digits(std::string_view s) {
    std::vector<uint8_t> v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<uint8_t>(c - '0'));
    return v;
}

std::string map_sequence(std::string_view digit_str) {
    auto d = parse_digits(digit_str);
    TwoDigitBlockMapper mapper;
    std::vector<char> out(d.size() / 2);
    std::size_t n = 0;
    mapper.map(d.data(), d.size(), out.data(), n);
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n)};
}

AhoCorasickCPU build_finder(int min_word_length) {
    AhoCorasickCPU ac;
    ac.set_min_word_length(min_word_length);
    ac.load_dictionary(DICT_PATH);
    ac.build();
    return ac;
}

}  // namespace

// ── Sequence 1: 191919190022070011041405001900110419191919
//    Letters: ttttawhaleofataletttt
//    Words (min=2): taw hale of at ale — all gaps 0, one phrase

TEST(DigitSeqIntegration, Seq1_LetterSequence) {
    EXPECT_EQ(map_sequence("191919190022070011041405001900110419191919"),
              "ttttawhaleofataletttt");
}

TEST(DigitSeqIntegration, Seq1_WordsFound) {
    const std::string letters = "ttttawhaleofataletttt";
    auto ac = build_finder(2);
    auto w = ac.scan(letters.data(), letters.size(), 0);

    ASSERT_EQ(w[0].size(), 5u);
    EXPECT_EQ(w[0][0].word, "taw");   EXPECT_EQ(w[0][0].start,  3u); EXPECT_FALSE(w[0][0].consecutive);
    EXPECT_EQ(w[0][1].word, "hale");  EXPECT_EQ(w[0][1].start,  6u); EXPECT_TRUE(w[0][1].consecutive);
    EXPECT_EQ(w[0][2].word, "of");    EXPECT_EQ(w[0][2].start, 10u); EXPECT_TRUE(w[0][2].consecutive);
    EXPECT_EQ(w[0][3].word, "at");    EXPECT_EQ(w[0][3].start, 12u); EXPECT_TRUE(w[0][3].consecutive);
    EXPECT_EQ(w[0][4].word, "ale");   EXPECT_EQ(w[0][4].start, 14u); EXPECT_TRUE(w[0][4].consecutive);
}

TEST(DigitSeqIntegration, Seq1_PhrasesFound) {
    const std::string letters = "ttttawhaleofataletttt";
    auto ac = build_finder(2);
    auto words = ac.scan(letters.data(), letters.size(), 0);
    HumanReviewScanner hs;
    auto phrases = hs.process_words(words[0]);

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 3u);
    EXPECT_EQ(phrases[0].words,     (std::vector<std::string>{"taw","hale","of","at","ale"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0, 0, 0, 0}));
}

// ── Sequence 2: 1616161622070011041405001900110416161616
//    Letters: qqqqwhaleofataleqqqq
//    Words (min=2): whale of at ale — all gaps 0, one phrase

TEST(DigitSeqIntegration, Seq2_LetterSequence) {
    EXPECT_EQ(map_sequence("1616161622070011041405001900110416161616"),
              "qqqqwhaleofataleqqqq");
}

TEST(DigitSeqIntegration, Seq2_WordsFound) {
    const std::string letters = "qqqqwhaleofataleqqqq";
    auto ac = build_finder(2);
    auto w = ac.scan(letters.data(), letters.size(), 0);

    ASSERT_EQ(w[0].size(), 4u);
    EXPECT_EQ(w[0][0].word, "whale"); EXPECT_EQ(w[0][0].start,  4u); EXPECT_FALSE(w[0][0].consecutive);
    EXPECT_EQ(w[0][1].word, "of");    EXPECT_EQ(w[0][1].start,  9u); EXPECT_TRUE(w[0][1].consecutive);
    EXPECT_EQ(w[0][2].word, "at");    EXPECT_EQ(w[0][2].start, 11u); EXPECT_TRUE(w[0][2].consecutive);
    EXPECT_EQ(w[0][3].word, "ale");   EXPECT_EQ(w[0][3].start, 13u); EXPECT_TRUE(w[0][3].consecutive);
}

TEST(DigitSeqIntegration, Seq2_PhrasesFound) {
    const std::string letters = "qqqqwhaleofataleqqqq";
    auto ac = build_finder(2);
    auto words = ac.scan(letters.data(), letters.size(), 0);
    HumanReviewScanner hs;
    auto phrases = hs.process_words(words[0]);

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 4u);
    EXPECT_EQ(phrases[0].words,     (std::vector<std::string>{"whale","of","at","ale"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0, 0, 0}));
}

// ── Sequence 3: 4216424248070011046605001900110416161616
//    Letters: qqqqwhaleofataleqqqq  (same output, different digit encoding)
//    Words (min=2): whale of at ale — same as seq 2

TEST(DigitSeqIntegration, Seq3_LetterSequence) {
    EXPECT_EQ(map_sequence("4216424248070011046605001900110416161616"),
              "qqqqwhaleofataleqqqq");
}

TEST(DigitSeqIntegration, Seq3_WordsFound) {
    const std::string letters = "qqqqwhaleofataleqqqq";
    auto ac = build_finder(2);
    auto w = ac.scan(letters.data(), letters.size(), 0);

    ASSERT_EQ(w[0].size(), 4u);
    EXPECT_EQ(w[0][0].word, "whale"); EXPECT_EQ(w[0][0].start,  4u); EXPECT_FALSE(w[0][0].consecutive);
    EXPECT_EQ(w[0][1].word, "of");    EXPECT_EQ(w[0][1].start,  9u); EXPECT_TRUE(w[0][1].consecutive);
    EXPECT_EQ(w[0][2].word, "at");    EXPECT_EQ(w[0][2].start, 11u); EXPECT_TRUE(w[0][2].consecutive);
    EXPECT_EQ(w[0][3].word, "ale");   EXPECT_EQ(w[0][3].start, 13u); EXPECT_TRUE(w[0][3].consecutive);
}

TEST(DigitSeqIntegration, Seq3_PhrasesFound) {
    const std::string letters = "qqqqwhaleofataleqqqq";
    auto ac = build_finder(2);
    auto words = ac.scan(letters.data(), letters.size(), 0);
    HumanReviewScanner hs;
    auto phrases = hs.process_words(words[0]);

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 4u);
    EXPECT_EQ(phrases[0].words,     (std::vector<std::string>{"whale","of","at","ale"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0, 0, 0}));
}

// ── Sequence 4: 001712141704
//    Letters: armore
//    Words (min=1): armor e — gap 0, consecutive

TEST(DigitSeqIntegration, Seq4_LetterSequence) {
    EXPECT_EQ(map_sequence("001712141704"), "armore");
}

TEST(DigitSeqIntegration, Seq4_WordsFound) {
    const std::string letters = "armore";
    auto ac = build_finder(1);
    auto w = ac.scan(letters.data(), letters.size(), 0);

    ASSERT_EQ(w[0].size(), 2u);
    EXPECT_EQ(w[0][0].word, "armor"); EXPECT_EQ(w[0][0].start, 0u); EXPECT_FALSE(w[0][0].consecutive);
    EXPECT_EQ(w[0][1].word, "e");     EXPECT_EQ(w[0][1].start, 5u); EXPECT_TRUE(w[0][1].consecutive);
}

TEST(DigitSeqIntegration, Seq4_PhrasesFound) {
    const std::string letters = "armore";
    auto ac = build_finder(1);
    auto words = ac.scan(letters.data(), letters.size(), 0);
    HumanReviewScanner hs;
    auto phrases = hs.process_words(words[0]);

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_EQ(phrases[0].words,     (std::vector<std::string>{"armor","e"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0}));
}

// ── Sequence 5: 0511000618190005050417
//    Letters: flagstaffer
//    Words (min=1): flagstaff er — gap 0, consecutive

TEST(DigitSeqIntegration, Seq5_LetterSequence) {
    EXPECT_EQ(map_sequence("0511000618190005050417"), "flagstaffer");
}

TEST(DigitSeqIntegration, Seq5_WordsFound) {
    const std::string letters = "flagstaffer";
    auto ac = build_finder(1);
    auto w = ac.scan(letters.data(), letters.size(), 0);

    ASSERT_EQ(w[0].size(), 2u);
    EXPECT_EQ(w[0][0].word, "flagstaff"); EXPECT_EQ(w[0][0].start, 0u); EXPECT_FALSE(w[0][0].consecutive);
    EXPECT_EQ(w[0][1].word, "er");        EXPECT_EQ(w[0][1].start, 9u); EXPECT_TRUE(w[0][1].consecutive);
}

TEST(DigitSeqIntegration, Seq5_PhrasesFound) {
    const std::string letters = "flagstaffer";
    auto ac = build_finder(1);
    auto words = ac.scan(letters.data(), letters.size(), 0);
    HumanReviewScanner hs;
    auto phrases = hs.process_words(words[0]);

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_EQ(phrases[0].words,     (std::vector<std::string>{"flagstaff","er"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0}));
}

// ── Sequence 6: 051100060618190005050417
//    Letters: flaggstaffer
//    Words (min=2): flag gs ta ff er — all consecutive (gs, ta, ff are 2-letter
//    words in english.txt that fill the gap, so "staffer" is never reached)

TEST(DigitSeqIntegration, Seq6_LetterSequence) {
    EXPECT_EQ(map_sequence("051100060618190005050417"), "flaggstaffer");
}

TEST(DigitSeqIntegration, Seq6_WordsFound) {
    const std::string letters = "flaggstaffer";
    auto ac = build_finder(2);
    auto w = ac.scan(letters.data(), letters.size(), 0);

    ASSERT_EQ(w[0].size(), 5u);
    EXPECT_EQ(w[0][0].word, "flag"); EXPECT_EQ(w[0][0].start,  0u); EXPECT_FALSE(w[0][0].consecutive);
    EXPECT_EQ(w[0][1].word, "gs");   EXPECT_EQ(w[0][1].start,  4u); EXPECT_TRUE(w[0][1].consecutive);
    EXPECT_EQ(w[0][2].word, "ta");   EXPECT_EQ(w[0][2].start,  6u); EXPECT_TRUE(w[0][2].consecutive);
    EXPECT_EQ(w[0][3].word, "ff");   EXPECT_EQ(w[0][3].start,  8u); EXPECT_TRUE(w[0][3].consecutive);
    EXPECT_EQ(w[0][4].word, "er");   EXPECT_EQ(w[0][4].start, 10u); EXPECT_TRUE(w[0][4].consecutive);
}

TEST(DigitSeqIntegration, Seq6_PhrasesFound) {
    const std::string letters = "flaggstaffer";
    auto ac = build_finder(2);
    auto words = ac.scan(letters.data(), letters.size(), 0);
    HumanReviewScanner hs;
    auto phrases = hs.process_words(words[0]);

    ASSERT_EQ(phrases.size(), 1u);
    EXPECT_EQ(phrases[0].start_offset, 0u);
    EXPECT_EQ(phrases[0].words,     (std::vector<std::string>{"flag","gs","ta","ff","er"}));
    EXPECT_EQ(phrases[0].gap_sizes, (std::vector<int>{0, 0, 0, 0}));
}
