#pragma once

#include <string_view>

#include "rangeedit/types.hpp"

namespace rangeedit {

[[nodiscard]] MatchList find_literal(
    std::string_view haystack,
    std::string_view needle,
    const MatchOptions& options = {},
    LiteralAlgorithm algorithm = LiteralAlgorithm::Bmh);

[[nodiscard]] MatchList find_literal(
    std::u16string_view haystack,
    std::u16string_view needle,
    const MatchOptions& options = {},
    LiteralAlgorithm algorithm = LiteralAlgorithm::Bmh);

[[nodiscard]] const char* to_string(LiteralAlgorithm algorithm) noexcept;

}  // namespace rangeedit
