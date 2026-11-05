#include "rangeedit/edit.hpp"
#include "rangeedit/literal.hpp"
#if defined(RANGEEDIT_HAS_REGEX)
#include "rangeedit/regex.hpp"
#endif

#include <gtest/gtest.h>

#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using rangeedit::EditList;
using rangeedit::EditMode;
using rangeedit::EditRange;

TEST(EditTest, PlansReplacementRangesAndStats) {
    const std::string input = "hello world";
    const std::vector<rangeedit::Match> matches = {{0, 5}, {6, 5}};

    const auto plan = rangeedit::plan_replace(input, matches, "hi");

    ASSERT_TRUE(plan.ok()) << plan.error;
    EXPECT_EQ(plan.edits, (EditList{{0, 5}, {6, 5}}));
    EXPECT_EQ(plan.stats.edit_count, 2U);
    EXPECT_EQ(plan.stats.removed_units, 10U);
    EXPECT_EQ(plan.stats.inserted_units, 4U);
    EXPECT_EQ(plan.stats.output_units, 5U);
}

TEST(EditTest, DeleteModeIgnoresReplacement) {
    const std::string input = "abcabc";
    const std::vector<rangeedit::Match> matches = {{0, 3}, {3, 3}};

    const auto plan = rangeedit::plan_edits(input, matches, "ignored", EditMode::Delete);

    ASSERT_TRUE(plan.ok()) << plan.error;
    EXPECT_EQ(plan.stats.edit_count, 2U);
    EXPECT_EQ(plan.stats.removed_units, 6U);
    EXPECT_EQ(plan.stats.inserted_units, 0U);
    EXPECT_EQ(plan.stats.output_units, 0U);

    const auto result = rangeedit::apply_edits(input, plan.edits, "");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, "");

    const auto delete_plan = rangeedit::plan_delete(input, matches);
    ASSERT_TRUE(delete_plan.ok()) << delete_plan.error;
    EXPECT_EQ(delete_plan.edits, plan.edits);
}

TEST(EditTest, RejectsOutOfBoundsStart) {
    const std::string input = "abc";
    const std::vector<rangeedit::Match> matches = {{4, 0}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit range is out of bounds");
    EXPECT_TRUE(plan.edits.empty());
}

TEST(EditTest, RejectsOutOfBoundsLength) {
    const std::string input = "abc";
    const std::vector<rangeedit::Match> matches = {{2, 2}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit range is out of bounds");
}

TEST(EditTest, RejectsUnsortedRanges) {
    const std::string input = "abcdef";
    const std::vector<rangeedit::Match> matches = {{3, 1}, {1, 1}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit ranges must have strictly increasing starts and must not overlap");
}

TEST(EditTest, RejectsOverlappingRanges) {
    const std::string input = "abcdef";
    const std::vector<rangeedit::Match> matches = {{0, 3}, {2, 1}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit ranges must have strictly increasing starts and must not overlap");
}

TEST(EditTest, RejectsDuplicateInsertionPoints) {
    const std::string input = "abc";
    const std::vector<rangeedit::Match> matches = {{1, 0}, {1, 0}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit ranges must have strictly increasing starts and must not overlap");
}

TEST(EditTest, AllowsAdjacentRanges) {
    const std::string input = "abcd";
    const std::vector<rangeedit::Match> matches = {{0, 2}, {2, 2}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    ASSERT_TRUE(plan.ok()) << plan.error;
    EXPECT_EQ(plan.edits, (EditList{{0, 2}, {2, 2}}));
}

TEST(EditTest, AllowsZeroLengthInsertion) {
    const std::string input = "abcd";
    const std::vector<rangeedit::Match> matches = {{2, 0}};

    const auto plan = rangeedit::plan_replace(input, matches, "XY");

    ASSERT_TRUE(plan.ok()) << plan.error;
    EXPECT_EQ(plan.stats.removed_units, 0U);
    EXPECT_EQ(plan.stats.inserted_units, 2U);
    EXPECT_EQ(plan.stats.output_units, 6U);

    const auto result = rangeedit::apply_edits(input, plan.edits, "XY");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, "abXYcd");
}

TEST(EditTest, RejectsUtf8StartInsideSequence) {
    const std::string input = "\xC3\xA9";
    const std::vector<rangeedit::Match> matches = {{1, 0}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit range splits a UTF-8 sequence");
}

TEST(EditTest, RejectsUtf8EndInsideSequence) {
    const std::string input = "\xC3\xA9";
    const std::vector<rangeedit::Match> matches = {{0, 1}};

    const auto plan = rangeedit::plan_replace(input, matches, "x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit range splits a UTF-8 sequence");
}

TEST(EditTest, AllowsUtf8MultibyteRange) {
    const std::string input = "a\xC3\xA9z";
    const std::vector<rangeedit::Match> matches = {{1, 2}};

    const auto plan = rangeedit::plan_replace(input, matches, "e");

    ASSERT_TRUE(plan.ok()) << plan.error;
    const auto result = rangeedit::apply_edits(input, plan.edits, "e");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, "aez");
}

TEST(EditTest, RejectsUtf16StartInsideSurrogatePair) {
    const std::u16string input = u"\U0001F600";
    const std::vector<rangeedit::Match> matches = {{1, 0}};

    const auto plan = rangeedit::plan_replace(input, matches, u"x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit range splits a UTF-16 surrogate pair");
}

TEST(EditTest, RejectsUtf16EndInsideSurrogatePair) {
    const std::u16string input = u"\U0001F600";
    const std::vector<rangeedit::Match> matches = {{0, 1}};

    const auto plan = rangeedit::plan_replace(input, matches, u"x");

    EXPECT_FALSE(plan.ok());
    EXPECT_EQ(plan.error, "edit range splits a UTF-16 surrogate pair");
}

TEST(EditTest, AllowsUtf16SurrogatePairRange) {
    const std::u16string input = u"\U0001F600";
    const std::vector<rangeedit::Match> matches = {{0, 2}};

    const auto plan = rangeedit::plan_replace(input, matches, u"x");

    ASSERT_TRUE(plan.ok()) << plan.error;
    const auto result = rangeedit::apply_edits(input, plan.edits, u"x");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, u"x");
}

TEST(EditTest, AppliesUtf8Replacements) {
    const std::string input = "one two three";
    const std::vector<rangeedit::Match> matches = {{0, 3}, {8, 5}};

    const auto plan = rangeedit::plan_replace(input, matches, "X");
    ASSERT_TRUE(plan.ok()) << plan.error;

    const auto result = rangeedit::apply_edits(input, plan.edits, "X");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, "X two X");
}

TEST(EditTest, AppliesUtf16Replacements) {
    const std::u16string input = u"one two three";
    const std::vector<rangeedit::Match> matches = {{0, 3}, {8, 5}};

    const auto plan = rangeedit::plan_replace(input, matches, u"X");
    ASSERT_TRUE(plan.ok()) << plan.error;

    const auto result = rangeedit::apply_edits(input, plan.edits, u"X");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, u"X two X");
}

TEST(EditTest, ApplyRejectsInvalidRanges) {
    const std::string input = "abc";
    const EditList edits = {{0, 5}};

    const auto result = rangeedit::apply_edits(input, edits, "x");

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, "edit range is out of bounds");
    EXPECT_TRUE(result.text.empty());
}

TEST(EditTest, EmptyEditsReturnOriginalInput) {
    const std::string input = "abc";

    const auto result = rangeedit::apply_edits(input, EditList{}, "x");

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, input);
}

TEST(EditTest, AppliesLiteralMatchesEndToEnd) {
    const std::string input = "hello world";
    const auto matches = rangeedit::find_literal(input, "o");

    const auto plan = rangeedit::plan_replace(input, matches, "0");
    ASSERT_TRUE(plan.ok()) << plan.error;

    const auto result = rangeedit::apply_edits(input, plan.edits, "0");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, "hell0 w0rld");
}

#if defined(RANGEEDIT_HAS_REGEX)
TEST(EditTest, AppliesRegexZeroLengthMatchesEndToEnd) {
    const std::string input = "abc";
    const auto regex = rangeedit::find_regex(input, "^");
    ASSERT_TRUE(regex.ok()) << regex.error;
    ASSERT_EQ(regex.matches.size(), 1U);

    const auto plan = rangeedit::plan_replace(input, regex.matches, "X");
    ASSERT_TRUE(plan.ok()) << plan.error;

    const auto result = rangeedit::apply_edits(input, plan.edits, "X");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.text, "Xabc");
}
#endif

TEST(EditTest, RandomizedApplyMatchesReference) {
    std::mt19937 rng(12345);

    for (int iteration = 0; iteration < 500; ++iteration) {
        const auto input_length = static_cast<std::size_t>(rng() % 65);
        std::string input(input_length, 'a');
        for (auto& character : input) {
            character = static_cast<char>('a' + static_cast<int>(rng() % 4));
        }

        rangeedit::MatchList matches;
        std::size_t cursor = 0;
        while (cursor < input.size()) {
            const auto gap = static_cast<std::size_t>(rng() % 5);
            cursor += gap;
            if (cursor > input.size()) {
                break;
            }

            const auto length = static_cast<std::size_t>(rng() % 4);
            if (length > input.size() - cursor) {
                break;
            }

            matches.push_back({cursor, length});
            cursor += length == 0 ? 1 : length;
        }

        const std::string replacement = (rng() % 2) == 0 ? "" : "XY";
        const auto plan = rangeedit::plan_replace(input, matches, replacement);
        ASSERT_TRUE(plan.ok()) << plan.error;

        std::string expected;
        std::size_t expected_cursor = 0;
        for (const auto& edit : plan.edits) {
            expected.append(input.substr(expected_cursor, edit.start - expected_cursor));
            expected.append(replacement);
            expected_cursor = edit.start + edit.length;
        }
        expected.append(input.substr(expected_cursor));

        const auto result = rangeedit::apply_edits(input, plan.edits, replacement);
        ASSERT_TRUE(result.ok()) << result.error;
        EXPECT_EQ(result.text, expected);
        EXPECT_EQ(plan.stats.output_units, expected.size());
    }
}

}  // namespace
