#include "rangeedit/literal.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using rangeedit::LiteralAlgorithm;
using rangeedit::Match;
using rangeedit::MatchOptions;

constexpr std::array<LiteralAlgorithm, 4> kAlgorithms = {
    LiteralAlgorithm::Naive,
    LiteralAlgorithm::Bmh,
    LiteralAlgorithm::Kmp,
    LiteralAlgorithm::StdSearch,
};

template <typename CharT>
void expect_parity(
    std::basic_string_view<CharT> haystack,
    std::basic_string_view<CharT> needle,
    const MatchOptions& options) {
    const auto expected =
        rangeedit::find_literal(haystack, needle, options, LiteralAlgorithm::Naive);

    for (const auto algorithm : kAlgorithms) {
        const auto actual = rangeedit::find_literal(haystack, needle, options, algorithm);
        EXPECT_EQ(actual, expected)
            << "algorithm=" << rangeedit::to_string(algorithm)
            << " haystack_size=" << haystack.size()
            << " needle_size=" << needle.size()
            << " case_sensitive=" << options.case_sensitive
            << " whole_word=" << options.whole_word
            << " max_matches=" << options.max_matches;
    }
}

std::string make_binary_string(std::size_t length, unsigned int mask) {
    std::string text(length, 'a');
    for (std::size_t i = 0; i < length; ++i) {
        if (((mask >> i) & 1U) != 0U) {
            text[i] = 'b';
        }
    }
    return text;
}

TEST(LiteralTest, FindsSingleMatch) {
    const std::string haystack = "the quick brown fox";
    const std::string needle = "quick";

    const auto matches = rangeedit::find_literal(haystack, needle);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0], (Match{4, 5}));
}

TEST(LiteralTest, FindsAllNonOverlappingMatches) {
    const std::string haystack = "aaaa";
    const std::string needle = "aa";

    const auto matches = rangeedit::find_literal(haystack, needle);

    EXPECT_EQ(matches, (std::vector<Match>{{0, 2}, {2, 2}}));
}

TEST(LiteralTest, FindsNeedleAtEnd) {
    const std::string haystack = "abcabc";
    const std::string needle = "bc";

    const auto matches = rangeedit::find_literal(haystack, needle);

    EXPECT_EQ(matches, (std::vector<Match>{{1, 2}, {4, 2}}));
}

TEST(LiteralTest, EmptyNeedleProducesNoMatches) {
    const std::string haystack = "abc";

    const auto matches = rangeedit::find_literal(haystack, std::string_view{});

    EXPECT_TRUE(matches.empty());
}

TEST(LiteralTest, NeedleLongerThanHaystackProducesNoMatches) {
    const std::string haystack = "abc";
    const std::string needle = "abcd";

    const auto matches = rangeedit::find_literal(haystack, needle);

    EXPECT_TRUE(matches.empty());
}

TEST(LiteralTest, ZeroMaxMatchesProducesNoMatches) {
    const std::string haystack = "abcabc";
    const std::string needle = "abc";
    MatchOptions options;
    options.max_matches = 0;

    const auto matches = rangeedit::find_literal(haystack, needle, options);

    EXPECT_TRUE(matches.empty());
}

TEST(LiteralTest, HonorsMaxMatches) {
    const std::string haystack = "a a a a";
    const std::string needle = "a";
    MatchOptions options;
    options.max_matches = 2;

    const auto matches = rangeedit::find_literal(haystack, needle, options);

    EXPECT_EQ(matches, (std::vector<Match>{{0, 1}, {2, 1}}));
}

TEST(LiteralTest, CaseSensitiveByDefault) {
    const std::string haystack = "AbCabc";
    const std::string needle = "abc";

    const auto matches = rangeedit::find_literal(haystack, needle);

    EXPECT_EQ(matches, (std::vector<Match>{{3, 3}}));
}

TEST(LiteralTest, CaseInsensitiveFoldIsAsciiOnly) {
    const std::string haystack = "AbC \xC3\x84" "bc abc";
    const std::string needle = "abc";
    MatchOptions options;
    options.case_sensitive = false;

    const auto matches = rangeedit::find_literal(haystack, needle, options);

    EXPECT_EQ(matches, (std::vector<Match>{{0, 3}, {9, 3}}));
}

TEST(LiteralTest, WholeWordRejectsEmbeddedMatches) {
    const std::string haystack = "cat scatter cat";
    const std::string needle = "cat";
    MatchOptions options;
    options.whole_word = true;

    const auto matches = rangeedit::find_literal(haystack, needle, options);

    EXPECT_EQ(matches, (std::vector<Match>{{0, 3}, {12, 3}}));
}

TEST(LiteralTest, WholeWordWithoutFlagFindsEmbeddedMatches) {
    const std::string haystack = "cat scatter cat";
    const std::string needle = "cat";

    const auto matches = rangeedit::find_literal(haystack, needle);

    EXPECT_EQ(matches, (std::vector<Match>{{0, 3}, {5, 3}, {12, 3}}));
}

TEST(LiteralTest, WholeWordUsesAsciiWordCharacters) {
    const std::string haystack = "cat_cat cat2 cat";
    const std::string needle = "cat";
    MatchOptions options;
    options.whole_word = true;

    const auto matches = rangeedit::find_literal(haystack, needle, options);

    EXPECT_EQ(matches, (std::vector<Match>{{13, 3}}));
}

TEST(LiteralTest, SupportsUtf16CodeUnits) {
    const std::u16string haystack = u"Hello \U0001F600 world Hello";
    const std::u16string needle = u"Hello";

    const auto matches = rangeedit::find_literal(haystack, needle);

    EXPECT_EQ(matches, (std::vector<Match>{{0, 5}, {15, 5}}));
}

TEST(LiteralTest, Utf16WholeWordAndCaseInsensitive) {
    const std::u16string haystack = u"Cat cat scatter CAT";
    const std::u16string needle = u"cat";
    MatchOptions options;
    options.case_sensitive = false;
    options.whole_word = true;

    const auto matches = rangeedit::find_literal(haystack, needle, options);

    EXPECT_EQ(matches, (std::vector<Match>{{0, 3}, {4, 3}, {16, 3}}));
}

TEST(LiteralTest, AlgorithmsAgreeOnExhaustiveBinaryInputs) {
    for (std::size_t haystack_length = 0; haystack_length <= 8; ++haystack_length) {
        const unsigned int haystack_count = 1U << haystack_length;

        for (unsigned int haystack_mask = 0; haystack_mask < haystack_count; ++haystack_mask) {
            const auto haystack = make_binary_string(haystack_length, haystack_mask);

            for (std::size_t needle_length = 0; needle_length <= 4; ++needle_length) {
                const unsigned int needle_count = 1U << needle_length;

                for (unsigned int needle_mask = 0; needle_mask < needle_count; ++needle_mask) {
                    const auto needle = make_binary_string(needle_length, needle_mask);

                    for (const bool case_sensitive : {true, false}) {
                        for (const bool whole_word : {true, false}) {
                            MatchOptions options;
                            options.case_sensitive = case_sensitive;
                            options.whole_word = whole_word;
                            expect_parity(
                                std::string_view(haystack),
                                std::string_view(needle),
                                options);
                        }
                    }
                }
            }
        }
    }
}

TEST(LiteralTest, AlgorithmsAgreeOnRandomInputs) {
    std::mt19937 generator(0xC0FFEEU);
    std::uniform_int_distribution<std::size_t> length_distribution(0, 64);
    std::uniform_int_distribution<int> character_distribution(0, 5);

    constexpr std::array<char, 6> alphabet = {'a', 'A', 'b', 'B', 'c', ' '};

    for (int iteration = 0; iteration < 500; ++iteration) {
        const auto haystack_length = length_distribution(generator);
        const auto needle_length = length_distribution(generator) % 8U;

        std::string haystack;
        haystack.reserve(haystack_length);
        for (std::size_t i = 0; i < haystack_length; ++i) {
            const auto index = static_cast<std::size_t>(character_distribution(generator));
            haystack.push_back(alphabet[index]);
        }

        std::string needle;
        needle.reserve(needle_length);
        for (std::size_t i = 0; i < needle_length; ++i) {
            const auto index = static_cast<std::size_t>(character_distribution(generator));
            needle.push_back(alphabet[index]);
        }

        for (const bool case_sensitive : {true, false}) {
            for (const bool whole_word : {true, false}) {
                for (const std::size_t max_matches : {std::size_t{0}, std::size_t{1}, std::size_t{5},
                                                      std::numeric_limits<std::size_t>::max()}) {
                    MatchOptions options;
                    options.case_sensitive = case_sensitive;
                    options.whole_word = whole_word;
                    options.max_matches = max_matches;
                    expect_parity(
                        std::string_view(haystack),
                        std::string_view(needle),
                        options);
                }
            }
        }
    }
}

TEST(LiteralTest, ReportsProgressForLongScan) {
    const std::string haystack(3 * 1024 * 1024, 'a');
    MatchOptions options;
    std::vector<std::size_t> progress;

    options.progress_callback = [&progress](std::size_t processed) {
        progress.push_back(processed);
    };

    const auto matches = rangeedit::find_literal(haystack, std::string_view("zz"), options);

    EXPECT_TRUE(matches.empty());
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.front(), 1024U * 1024U);
    for (std::size_t i = 1; i < progress.size(); ++i) {
        EXPECT_LT(progress[i - 1], progress[i]);
    }
}

TEST(LiteralTest, CancellationStopsLongScan) {
    const std::string haystack(4 * 1024 * 1024, 'a');
    rangeedit::CancellationToken token;
    MatchOptions options;
    options.cancellation_token = &token;
    std::size_t progress_calls = 0;

    options.progress_callback = [&token, &progress_calls](std::size_t processed) {
        ++progress_calls;
        if (processed >= 1024U * 1024U) {
            token.cancel();
        }
    };

    const auto matches = rangeedit::find_literal(haystack, std::string_view("zz"), options);

    EXPECT_TRUE(token.cancelled());
    EXPECT_TRUE(matches.empty());
    EXPECT_EQ(progress_calls, 1U);
}

}  // namespace
