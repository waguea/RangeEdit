#include "rangeedit/literal.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <vector>

namespace rangeedit {
namespace {

class ScanMonitor {
public:
    explicit ScanMonitor(const MatchOptions& options) noexcept : options_(options) {}

    [[nodiscard]] bool tick(std::size_t processed) {
        if (processed >= next_check_) {
            next_check_ = processed + kCheckInterval;
            if (options_.cancellation_token != nullptr &&
                options_.cancellation_token->cancelled()) {
                return false;
            }
        }

        if (options_.progress_callback && processed >= next_progress_) {
            next_progress_ = processed + kProgressInterval;
            options_.progress_callback(processed);
            if (options_.cancellation_token != nullptr &&
                options_.cancellation_token->cancelled()) {
                return false;
            }
        }

        return true;
    }

private:
    static constexpr std::size_t kCheckInterval = 16 * 1024;
    static constexpr std::size_t kProgressInterval = 1024 * 1024;

    const MatchOptions& options_;
    std::size_t next_check_ = 0;
    std::size_t next_progress_ = kProgressInterval;
};

template <typename CharT>
constexpr CharT fold_ascii(CharT value) noexcept {
    if constexpr (std::is_same_v<CharT, char>) {
        const auto c = static_cast<unsigned char>(value);
        if (c >= static_cast<unsigned char>('A') && c <= static_cast<unsigned char>('Z')) {
            const auto folded = static_cast<unsigned char>(
                c - static_cast<unsigned char>('A') + static_cast<unsigned char>('a'));
            return static_cast<CharT>(folded);
        }
        return value;
    } else {
        if (value >= u'A' && value <= u'Z') {
            return static_cast<CharT>(value - u'A' + u'a');
        }
        return value;
    }
}

template <typename CharT>
constexpr bool is_word_ascii(CharT value) noexcept {
    if constexpr (std::is_same_v<CharT, char>) {
        const auto c = static_cast<unsigned char>(value);
        return (c >= static_cast<unsigned char>('A') && c <= static_cast<unsigned char>('Z')) ||
               (c >= static_cast<unsigned char>('a') && c <= static_cast<unsigned char>('z')) ||
               (c >= static_cast<unsigned char>('0') && c <= static_cast<unsigned char>('9')) ||
               c == static_cast<unsigned char>('_');
    } else {
        return (value >= u'A' && value <= u'Z') || (value >= u'a' && value <= u'z') ||
               (value >= u'0' && value <= u'9') || value == u'_';
    }
}

template <typename CharT>
constexpr bool chars_equal(CharT left, CharT right, bool case_sensitive) noexcept {
    if (case_sensitive) {
        return left == right;
    }
    return fold_ascii(left) == fold_ascii(right);
}

template <typename CharT>
[[nodiscard]] bool matches_at(
    std::basic_string_view<CharT> haystack,
    std::basic_string_view<CharT> needle,
    std::size_t position,
    bool case_sensitive) noexcept {
    for (std::size_t i = 0; i < needle.size(); ++i) {
        if (!chars_equal(haystack[position + i], needle[i], case_sensitive)) {
            return false;
        }
    }
    return true;
}

template <typename CharT>
[[nodiscard]] bool is_acceptable(
    std::basic_string_view<CharT> haystack,
    std::size_t start,
    std::size_t length,
    const MatchOptions& options) noexcept {
    if (!options.whole_word) {
        return true;
    }

    if (start > 0 && is_word_ascii(haystack[start - 1])) {
        return false;
    }

    const auto end = start + length;
    if (end < haystack.size() && is_word_ascii(haystack[end])) {
        return false;
    }

    return true;
}

template <typename CharT>
MatchList find_naive(
    std::basic_string_view<CharT> haystack,
    std::basic_string_view<CharT> needle,
    const MatchOptions& options) {
    MatchList matches;

    const auto needle_length = needle.size();
    if (needle_length == 0 || needle_length > haystack.size() || options.max_matches == 0) {
        return matches;
    }

    ScanMonitor monitor(options);
    for (std::size_t position = 0; position + needle_length <= haystack.size();) {
        if (!monitor.tick(position)) {
            break;
        }

        if (matches_at(haystack, needle, position, options.case_sensitive) &&
            is_acceptable(haystack, position, needle_length, options)) {
            matches.push_back({position, needle_length});
            if (matches.size() >= options.max_matches) {
                break;
            }
            position += needle_length;
        } else {
            ++position;
        }
    }

    return matches;
}

template <typename CharT>
class BadCharacterTable {
public:
    BadCharacterTable(std::basic_string_view<CharT> pattern, bool case_sensitive)
        : table_(kAlphabetSize, pattern.size()) {
        const auto last_index = pattern.size() - 1;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            auto c = pattern[i];
            if (!case_sensitive) {
                c = fold_ascii(c);
            }
            table_[bucket(c)] = last_index - i;
        }
    }

    [[nodiscard]] std::size_t shift(CharT value) const noexcept {
        return std::max<std::size_t>(1, table_[bucket(value)]);
    }

private:
    static_assert(sizeof(CharT) <= 2, "BadCharacterTable supports char and char16_t");
    static constexpr std::size_t kAlphabetSize = sizeof(CharT) == 1 ? 256 : 65536;

    [[nodiscard]] static std::size_t bucket(CharT value) noexcept {
        if constexpr (std::is_same_v<CharT, char>) {
            return static_cast<std::size_t>(static_cast<unsigned char>(value));
        } else {
            return static_cast<std::size_t>(value);
        }
    }

    std::vector<std::size_t> table_;
};

template <typename CharT>
MatchList find_bmh(
    std::basic_string_view<CharT> haystack,
    std::basic_string_view<CharT> needle,
    const MatchOptions& options) {
    MatchList matches;

    const auto needle_length = needle.size();
    if (needle_length == 0 || needle_length > haystack.size() || options.max_matches == 0) {
        return matches;
    }

    const BadCharacterTable<CharT> table(needle, options.case_sensitive);

    ScanMonitor monitor(options);
    std::size_t position = 0;
    while (position + needle_length <= haystack.size()) {
        if (!monitor.tick(position)) {
            break;
        }

        if (matches_at(haystack, needle, position, options.case_sensitive)) {
            if (is_acceptable(haystack, position, needle_length, options)) {
                matches.push_back({position, needle_length});
                if (matches.size() >= options.max_matches) {
                    break;
                }
                position += needle_length;
            } else {
                ++position;
            }
        } else {
            auto last = haystack[position + needle_length - 1];
            if (!options.case_sensitive) {
                last = fold_ascii(last);
            }
            position += table.shift(last);
        }
    }

    return matches;
}

template <typename CharT>
[[nodiscard]] std::vector<std::size_t> build_lps(
    std::basic_string_view<CharT> pattern,
    bool case_sensitive) {
    std::vector<std::size_t> lps(pattern.size(), 0);

    std::size_t length = 0;
    std::size_t i = 1;
    while (i < pattern.size()) {
        if (chars_equal(pattern[i], pattern[length], case_sensitive)) {
            ++length;
            lps[i] = length;
            ++i;
        } else if (length != 0) {
            length = lps[length - 1];
        } else {
            lps[i] = 0;
            ++i;
        }
    }

    return lps;
}

template <typename CharT>
MatchList find_kmp(
    std::basic_string_view<CharT> haystack,
    std::basic_string_view<CharT> needle,
    const MatchOptions& options) {
    MatchList matches;

    const auto needle_length = needle.size();
    if (needle_length == 0 || needle_length > haystack.size() || options.max_matches == 0) {
        return matches;
    }

    const auto lps = build_lps(needle, options.case_sensitive);

    ScanMonitor monitor(options);
    std::size_t i = 0;
    std::size_t matched = 0;
    while (i < haystack.size()) {
        if (!monitor.tick(i)) {
            break;
        }

        if (chars_equal(haystack[i], needle[matched], options.case_sensitive)) {
            ++i;
            ++matched;

            if (matched == needle_length) {
                const auto start = i - needle_length;
                if (is_acceptable(haystack, start, needle_length, options)) {
                    matches.push_back({start, needle_length});
                    if (matches.size() >= options.max_matches) {
                        break;
                    }
                    matched = 0;
                } else {
                    i = start + 1;
                    matched = 0;
                }
            }
        } else if (matched != 0) {
            matched = lps[matched - 1];
        } else {
            ++i;
        }
    }

    return matches;
}

template <typename CharT>
MatchList find_std(
    std::basic_string_view<CharT> haystack,
    std::basic_string_view<CharT> needle,
    const MatchOptions& options) {
    MatchList matches;

    const auto needle_length = needle.size();
    if (needle_length == 0 || needle_length > haystack.size() || options.max_matches == 0) {
        return matches;
    }

    const auto equal = [case_sensitive = options.case_sensitive](CharT left, CharT right) noexcept {
        return chars_equal(left, right, case_sensitive);
    };

    ScanMonitor monitor(options);
    auto search_from = haystack.begin();
    while (search_from != haystack.end()) {
        const auto search_offset =
            static_cast<std::size_t>(std::distance(haystack.begin(), search_from));
        if (!monitor.tick(search_offset)) {
            break;
        }

        const auto found = std::search(
            search_from,
            haystack.end(),
            needle.begin(),
            needle.end(),
            equal);

        if (found == haystack.end()) {
            break;
        }

        const auto start = static_cast<std::size_t>(std::distance(haystack.begin(), found));
        if (is_acceptable(haystack, start, needle_length, options)) {
            matches.push_back({start, needle_length});
            if (matches.size() >= options.max_matches) {
                break;
            }
            search_from = found + static_cast<std::ptrdiff_t>(needle_length);
        } else {
            search_from = found + 1;
        }
    }

    return matches;
}

template <typename CharT>
MatchList find_literal_impl(
    std::basic_string_view<CharT> haystack,
    std::basic_string_view<CharT> needle,
    const MatchOptions& options,
    LiteralAlgorithm algorithm) {
    switch (algorithm) {
        case LiteralAlgorithm::Naive:
            return find_naive(haystack, needle, options);
        case LiteralAlgorithm::Bmh:
            return find_bmh(haystack, needle, options);
        case LiteralAlgorithm::Kmp:
            return find_kmp(haystack, needle, options);
        case LiteralAlgorithm::StdSearch:
            return find_std(haystack, needle, options);
    }

    return find_bmh(haystack, needle, options);
}

}  // namespace

MatchList find_literal(
    std::string_view haystack,
    std::string_view needle,
    const MatchOptions& options,
    LiteralAlgorithm algorithm) {
    return find_literal_impl(haystack, needle, options, algorithm);
}

MatchList find_literal(
    std::u16string_view haystack,
    std::u16string_view needle,
    const MatchOptions& options,
    LiteralAlgorithm algorithm) {
    return find_literal_impl(haystack, needle, options, algorithm);
}

const char* to_string(LiteralAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case LiteralAlgorithm::Naive:
            return "naive";
        case LiteralAlgorithm::Bmh:
            return "bmh";
        case LiteralAlgorithm::Kmp:
            return "kmp";
        case LiteralAlgorithm::StdSearch:
            return "std";
    }

    return "unknown";
}

}  // namespace rangeedit
