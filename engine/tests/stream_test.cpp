#include "rangeedit/literal.hpp"
#include "rangeedit/stream.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using rangeedit::LiteralAlgorithm;
using rangeedit::LiteralStreamScanner;
using rangeedit::Match;
using rangeedit::MatchOptions;

rangeedit::MatchList scan_chunks(
    std::string_view text,
    std::string_view needle,
    const MatchOptions& options,
    LiteralAlgorithm algorithm,
    std::size_t chunk_size) {
    LiteralStreamScanner scanner(std::string(needle), options, algorithm);

    rangeedit::MatchList matches;
    for (std::size_t offset = 0; offset < text.size(); offset += chunk_size) {
        const auto length = std::min(chunk_size, text.size() - offset);
        auto found = scanner.feed(text.substr(offset, length));
        matches.insert(matches.end(), found.begin(), found.end());
    }

    auto tail = scanner.finish();
    matches.insert(matches.end(), tail.begin(), tail.end());
    return matches;
}

TEST(StreamTest, FindsMatchAcrossChunkBoundary) {
    const std::string text = "abcneedlexyz";
    const std::string needle = "needle";

    const auto matches = scan_chunks(text, needle, {}, LiteralAlgorithm::Bmh, 6);

    EXPECT_EQ(matches, (rangeedit::MatchList{{3, 6}}));
}

TEST(StreamTest, WholeWordSeesCharacterBeforeChunkBoundary) {
    const std::string text = "xcat y";
    MatchOptions options;
    options.whole_word = true;

    const auto matches = scan_chunks(text, "cat", options, LiteralAlgorithm::Bmh, 3);

    EXPECT_TRUE(matches.empty());
}

TEST(StreamTest, WholeWordAcceptsMatchStartingInOverlap) {
    const std::string text = " cat y";
    MatchOptions options;
    options.whole_word = true;

    const auto matches = scan_chunks(text, "cat", options, LiteralAlgorithm::Bmh, 3);

    EXPECT_EQ(matches, (rangeedit::MatchList{{1, 3}}));
}

TEST(StreamTest, WholeWordWaitsForFollowingCharacter) {
    const std::string text = "cat scatter";
    MatchOptions options;
    options.whole_word = true;

    const auto matches = scan_chunks(text, "cat", options, LiteralAlgorithm::Bmh, 3);

    EXPECT_EQ(matches, (rangeedit::MatchList{{0, 3}}));
}

TEST(StreamTest, WholeWordResolvesMatchAtEndOfInput) {
    const std::string text = "cat";
    MatchOptions options;
    options.whole_word = true;

    LiteralStreamScanner scanner("cat", options);
    const auto first = scanner.feed(text);
    EXPECT_TRUE(first.empty());

    const auto tail = scanner.finish();
    EXPECT_EQ(tail, (rangeedit::MatchList{{0, 3}}));
}

TEST(StreamTest, HonorsMaxMatchesAcrossChunks) {
    const std::string text = "a a a a";
    MatchOptions options;
    options.max_matches = 2;

    const auto matches = scan_chunks(text, "a", options, LiteralAlgorithm::Bmh, 2);

    EXPECT_EQ(matches, (rangeedit::MatchList{{0, 1}, {2, 1}}));
    EXPECT_EQ(matches.size(), 2U);
}

TEST(StreamTest, EmptyNeedleProducesNoMatches) {
    const std::string text = "abc";

    LiteralStreamScanner scanner({});
    EXPECT_TRUE(scanner.feed(text).empty());
    EXPECT_TRUE(scanner.finish().empty());
}

TEST(StreamTest, SupportsCaseInsensitiveMatchingAcrossChunks) {
    const std::string text = "AbCabc";
    MatchOptions options;
    options.case_sensitive = false;

    const auto matches = scan_chunks(text, "abc", options, LiteralAlgorithm::Kmp, 2);

    EXPECT_EQ(matches, (rangeedit::MatchList{{0, 3}, {3, 3}}));
}

TEST(StreamTest, RandomizedChunkingMatchesWholeFileSearch) {
    std::mt19937 rng(67890);
    constexpr std::array<LiteralAlgorithm, 4> kAlgorithms = {
        LiteralAlgorithm::Naive,
        LiteralAlgorithm::Bmh,
        LiteralAlgorithm::Kmp,
        LiteralAlgorithm::StdSearch,
    };

    for (int iteration = 0; iteration < 500; ++iteration) {
        const auto text_length = static_cast<std::size_t>(rng() % 200);
        std::string text(text_length, 'a');
        for (auto& character : text) {
            constexpr std::string_view kAlphabet = "ab _\n";
            character = kAlphabet[rng() % kAlphabet.size()];
        }

        const auto needle_length = static_cast<std::size_t>(rng() % 6);
        std::string needle(needle_length, 'a');
        for (auto& character : needle) {
            constexpr std::string_view kAlphabet = "ab _";
            character = kAlphabet[rng() % kAlphabet.size()];
        }

        MatchOptions options;
        options.case_sensitive = (rng() % 2) == 0;
        options.whole_word = (rng() % 2) == 0;
        if ((rng() % 3) == 0) {
            options.max_matches = static_cast<std::size_t>(rng() % 8);
        }

        const auto algorithm = kAlgorithms[rng() % kAlgorithms.size()];
        const auto chunk_size = static_cast<std::size_t>(rng() % 17) + 1;

        const auto expected = rangeedit::find_literal(text, needle, options, algorithm);
        const auto actual = scan_chunks(text, needle, options, algorithm, chunk_size);

        EXPECT_EQ(actual, expected)
            << "iteration=" << iteration
            << " text_size=" << text.size()
            << " needle_size=" << needle.size()
            << " chunk_size=" << chunk_size
            << " case_sensitive=" << options.case_sensitive
            << " whole_word=" << options.whole_word
            << " max_matches=" << options.max_matches;
    }
}

}  // namespace
