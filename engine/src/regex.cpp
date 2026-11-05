#include "rangeedit/regex.hpp"

#include <re2/re2.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rangeedit {
namespace {

struct Utf8Conversion {
    std::string text;
    std::vector<std::size_t> utf16_offset_by_byte;
};

void append_utf8(std::string& output, char32_t codepoint) {
    const auto value = static_cast<std::uint32_t>(codepoint);

    if (value <= 0x7FU) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

Utf8Conversion to_utf8(std::u16string_view input) {
    Utf8Conversion result;
    result.text.reserve(input.size());
    result.utf16_offset_by_byte.reserve(input.size() + 1);

    std::size_t index = 0;
    while (index < input.size()) {
        const auto unit = static_cast<std::uint32_t>(input[index]);
        std::uint32_t codepoint = unit;
        std::size_t consumed = 1;

        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            if (index + 1 < input.size()) {
                const auto next = static_cast<std::uint32_t>(input[index + 1]);
                if (next >= 0xDC00U && next <= 0xDFFFU) {
                    codepoint = 0x10000U + ((unit - 0xD800U) << 10U) + (next - 0xDC00U);
                    consumed = 2;
                } else {
                    codepoint = 0xFFFDU;
                }
            } else {
                codepoint = 0xFFFDU;
            }
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            codepoint = 0xFFFDU;
        }

        const auto unit_start = index;
        append_utf8(result.text, static_cast<char32_t>(codepoint));

        while (result.utf16_offset_by_byte.size() < result.text.size()) {
            result.utf16_offset_by_byte.push_back(unit_start);
        }

        index += consumed;
    }

    result.utf16_offset_by_byte.push_back(input.size());
    return result;
}

std::size_t next_utf8_offset(std::string_view text, std::size_t position) {
    if (position >= text.size()) {
        return text.size() + 1;
    }

    const auto lead = static_cast<unsigned char>(text[position]);
    std::size_t length = 1;

    if ((lead & 0x80U) == 0U) {
        length = 1;
    } else if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
    }

    const auto next = position + length;
    return next > text.size() ? text.size() : next;
}

std::unique_ptr<RE2> compile_regex(
    std::string pattern,
    const MatchOptions& options,
    std::string& error) {
    if (options.whole_word) {
        pattern = "\\b(?:" + pattern + ")\\b";
    }

    RE2::Options re2_options;
    re2_options.set_log_errors(false);
    re2_options.set_case_sensitive(options.case_sensitive);
    re2_options.set_max_mem(64 * 1024 * 1024);

    auto regex = std::make_unique<RE2>(std::move(pattern), re2_options);
    if (!regex->ok()) {
        error = regex->error();
        return nullptr;
    }

    return regex;
}

MatchList find_matches(
    const RE2& regex,
    std::string_view text,
    const MatchOptions& options,
    const std::vector<std::size_t>* utf16_offset_by_byte) {
    MatchList matches;

    if (options.max_matches == 0) {
        return matches;
    }

    const auto map_offset = [utf16_offset_by_byte](std::size_t offset) -> std::size_t {
        if (utf16_offset_by_byte == nullptr) {
            return offset;
        }
        if (offset >= utf16_offset_by_byte->size()) {
            return utf16_offset_by_byte->back();
        }
        return (*utf16_offset_by_byte)[offset];
    };

    const re2::StringPiece input(text.data(), text.size());

    std::size_t search_start = 0;
    while (search_start <= input.size()) {
        if (options.cancellation_token != nullptr &&
            options.cancellation_token->cancelled()) {
            break;
        }

        re2::StringPiece match;
        if (!regex.Match(input, search_start, input.size(), RE2::UNANCHORED, &match, 1)) {
            break;
        }

        const auto byte_start = static_cast<std::size_t>(match.data() - input.data());
        const auto byte_end = byte_start + match.size();

        const auto unit_start = map_offset(byte_start);
        const auto unit_end = map_offset(byte_end);
        matches.push_back({unit_start, unit_end - unit_start});

        if (matches.size() >= options.max_matches) {
            break;
        }

        if (match.size() == 0) {
            const auto next = next_utf8_offset(text, byte_start);
            if (next <= search_start) {
                break;
            }
            search_start = next;
        } else {
            search_start = byte_end;
        }
    }

    return matches;
}

}  // namespace

RegexResult find_regex(
    std::string_view haystack,
    std::string_view pattern,
    const MatchOptions& options) {
    if (pattern.empty()) {
        return {};
    }

    std::string error;
    const auto regex = compile_regex(std::string(pattern), options, error);
    if (!regex) {
        return RegexResult{{}, std::move(error)};
    }

    return RegexResult{find_matches(*regex, haystack, options, nullptr), {}};
}

RegexResult find_regex(
    std::u16string_view haystack,
    std::u16string_view pattern,
    const MatchOptions& options) {
    if (pattern.empty()) {
        return {};
    }

    auto pattern_utf8 = to_utf8(pattern);

    std::string error;
    const auto regex = compile_regex(std::move(pattern_utf8.text), options, error);
    if (!regex) {
        return RegexResult{{}, std::move(error)};
    }

    auto haystack_utf8 = to_utf8(haystack);

    return RegexResult{
        find_matches(
            *regex,
            std::string_view(haystack_utf8.text),
            options,
            &haystack_utf8.utf16_offset_by_byte),
        {}};
}

}  // namespace rangeedit
