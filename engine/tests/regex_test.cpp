#include "rangeedit/regex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using rangeedit::Match;
using rangeedit::MatchOptions;

TEST(RegexTest, FindsBasicMatch) {
    const std::string haystack = "the quick brown fox";

    const auto result = rangeedit::find_regex(haystack, "q[a-z]+");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{4, 5}}));
}

TEST(RegexTest, FindsNonOverlappingMatches) {
    const std::string haystack = "aaaa";

    const auto result = rangeedit::find_regex(haystack, "aa");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 2}, {2, 2}}));
}

TEST(RegexTest, IsCaseSensitiveByDefault) {
    const std::string haystack = "Cat cat";

    const auto result = rangeedit::find_regex(haystack, "cat");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{4, 3}}));
}

TEST(RegexTest, SupportsCaseInsensitiveMatching) {
    const std::string haystack = "Cat cat";
    MatchOptions options;
    options.case_sensitive = false;

    const auto result = rangeedit::find_regex(haystack, "cat", options);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 3}, {4, 3}}));
}

TEST(RegexTest, SupportsWholeWordMatching) {
    const std::string haystack = "cat scatter cat";
    MatchOptions options;
    options.whole_word = true;

    const auto result = rangeedit::find_regex(haystack, "cat", options);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 3}, {12, 3}}));
}

TEST(RegexTest, WholeWordAppliesToAlternations) {
    const std::string haystack = "cat scat dog doghouse";
    MatchOptions options;
    options.whole_word = true;

    const auto result = rangeedit::find_regex(haystack, "cat|dog", options);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 3}, {9, 3}}));
}

TEST(RegexTest, WholeWordWithCaseInsensitiveMatching) {
    const std::string haystack = "Cat scatter CAT";
    MatchOptions options;
    options.case_sensitive = false;
    options.whole_word = true;

    const auto result = rangeedit::find_regex(haystack, "cat", options);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 3}, {12, 3}}));
}

TEST(RegexTest, ReportsByteOffsetsForUtf8Input) {
    const std::string haystack = "\xC3\x84" "abc";

    const auto result = rangeedit::find_regex(haystack, "abc");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{2, 3}}));
}

TEST(RegexTest, SupportsUtf16Input) {
    const std::u16string haystack = u"Hello world";

    const auto result = rangeedit::find_regex(haystack, u"w[a-z]+");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{6, 5}}));
}

TEST(RegexTest, MapsEmojiMatchBackToUtf16CodeUnits) {
    const std::u16string haystack = u"Hello \U0001F600 world";

    const auto result = rangeedit::find_regex(haystack, u"\U0001F600");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{6, 2}}));
}

TEST(RegexTest, MapsUtf16OffsetsAfterMultibyteCharacters) {
    const std::u16string haystack = u"\u00C4 abc";

    const auto result = rangeedit::find_regex(haystack, u"abc");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{2, 3}}));
}

TEST(RegexTest, HandlesMultiCodeUnitPatternMatches) {
    const std::u16string haystack = u"\U0001F600\U0001F600";

    const auto result = rangeedit::find_regex(haystack, u"\U0001F600+");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 4}}));
}

TEST(RegexTest, ReplacesLoneSurrogatesWithReplacementCharacter) {
    const std::u16string haystack = u"\xD800" "abc";

    const auto result = rangeedit::find_regex(haystack, u"abc");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{1, 3}}));
}

TEST(RegexTest, SupportsUnicodeCaseFolding) {
    const std::u16string haystack = u"\u00C4";
    MatchOptions options;
    options.case_sensitive = false;

    const auto result = rangeedit::find_regex(haystack, u"\u00E4", options);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 1}}));
}

TEST(RegexTest, EmptyPatternProducesNoMatches) {
    const std::string haystack = "abc";

    const auto result = rangeedit::find_regex(haystack, "");

    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.matches.empty());
}

TEST(RegexTest, HandlesZeroLengthMatchesAtBoundaries) {
    const std::string haystack = "abc";

    const auto result = rangeedit::find_regex(haystack, "^|$");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 0}, {3, 0}}));
}

TEST(RegexTest, HandlesZeroLengthMatchOnEmptyInput) {
    const std::string haystack;

    const auto result = rangeedit::find_regex(haystack, "^$");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 0}}));
}

TEST(RegexTest, HonorsMaxMatches) {
    const std::string haystack = "a b c d";
    MatchOptions options;
    options.max_matches = 2;

    const auto result = rangeedit::find_regex(haystack, "[a-z]", options);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.matches, (std::vector<Match>{{0, 1}, {2, 1}}));
}

TEST(RegexTest, ZeroMaxMatchesProducesNoMatches) {
    const std::string haystack = "abc";
    MatchOptions options;
    options.max_matches = 0;

    const auto result = rangeedit::find_regex(haystack, "abc", options);

    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.matches.empty());
}

TEST(RegexTest, ReportsInvalidPattern) {
    const std::string haystack = "abc";

    const auto result = rangeedit::find_regex(haystack, "[");

    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.matches.empty());
}

TEST(RegexTest, PreCancelledTokenProducesNoMatches) {
    const std::string haystack = "cat cat cat";
    rangeedit::CancellationToken token;
    token.cancel();

    MatchOptions options;
    options.cancellation_token = &token;

    const auto result = rangeedit::find_regex(haystack, "cat", options);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_TRUE(result.matches.empty());
}

}  // namespace
