#include "rangeedit/regex.hpp"

namespace rangeedit {
namespace {

RegexResult unavailable() {
    return RegexResult{{}, "regex support was not built (RANGEEDIT_BUILD_REGEX=OFF)"};
}

}  // namespace

RegexResult find_regex(
    std::string_view /*haystack*/,
    std::string_view /*pattern*/,
    const MatchOptions& /*options*/) {
    return unavailable();
}

RegexResult find_regex(
    std::u16string_view /*haystack*/,
    std::u16string_view /*pattern*/,
    const MatchOptions& /*options*/) {
    return unavailable();
}

}  // namespace rangeedit
