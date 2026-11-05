#pragma once

#include <string>
#include <string_view>

#include "rangeedit/types.hpp"

namespace rangeedit {

struct RegexResult {
    MatchList matches;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] RegexResult find_regex(
    std::string_view haystack,
    std::string_view pattern,
    const MatchOptions& options = {});

[[nodiscard]] RegexResult find_regex(
    std::u16string_view haystack,
    std::u16string_view pattern,
    const MatchOptions& options = {});

}  // namespace rangeedit
