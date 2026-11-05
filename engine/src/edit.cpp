#include "rangeedit/edit.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace rangeedit {
namespace {

template <typename CharT>
[[nodiscard]] bool is_boundary(
    std::basic_string_view<CharT> input,
    std::size_t position) noexcept {
    if (position == 0 || position == input.size()) {
        return true;
    }

    if constexpr (std::is_same_v<CharT, char>) {
        const auto byte = static_cast<unsigned char>(input[position]);
        return (byte & 0xC0U) != 0x80U;
    } else {
        const auto previous = input[position - 1];
        const auto next = input[position];
        const bool previous_is_high = previous >= u'\xD800' && previous <= u'\xDBFF';
        const bool next_is_low = next >= u'\xDC00' && next <= u'\xDFFF';
        return !(previous_is_high && next_is_low);
    }
}

template <typename CharT>
[[nodiscard]] const char* validate_ranges(
    std::basic_string_view<CharT> input,
    const EditList& edits) noexcept {
    std::size_t previous_start = 0;
    std::size_t previous_end = 0;

    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& edit = edits[i];

        if (edit.start > input.size() || edit.length > input.size() - edit.start) {
            return "edit range is out of bounds";
        }

        if (i > 0 && (edit.start <= previous_start || edit.start < previous_end)) {
            return "edit ranges must have strictly increasing starts and must not overlap";
        }

        if (!is_boundary(input, edit.start) || !is_boundary(input, edit.start + edit.length)) {
            if constexpr (std::is_same_v<CharT, char>) {
                return "edit range splits a UTF-8 sequence";
            } else {
                return "edit range splits a UTF-16 surrogate pair";
            }
        }

        previous_start = edit.start;
        previous_end = edit.start + edit.length;
    }

    return nullptr;
}

template <typename CharT>
[[nodiscard]] bool compute_stats(
    std::basic_string_view<CharT> input,
    const EditList& edits,
    std::basic_string_view<CharT> replacement,
    EditPlanStats& stats) noexcept {
    constexpr auto kMax = std::numeric_limits<std::size_t>::max();

    std::size_t removed = 0;
    for (const auto& edit : edits) {
        if (edit.length > kMax - removed) {
            return false;
        }
        removed += edit.length;
    }

    std::size_t inserted = 0;
    if (!replacement.empty()) {
        if (edits.size() > kMax / replacement.size()) {
            return false;
        }
        inserted = replacement.size() * edits.size();
    }

    const auto remaining = input.size() - removed;
    if (inserted > kMax - remaining) {
        return false;
    }

    stats.edit_count = edits.size();
    stats.removed_units = removed;
    stats.inserted_units = inserted;
    stats.output_units = remaining + inserted;
    return true;
}

template <typename CharT>
[[nodiscard]] EditPlan plan_impl(
    std::basic_string_view<CharT> input,
    const MatchList& matches,
    std::basic_string_view<CharT> replacement,
    EditMode mode) {
    EditPlan plan;
    plan.edits.reserve(matches.size());

    for (const auto& match : matches) {
        plan.edits.push_back({match.start, match.length});
    }

    const auto effective_replacement =
        mode == EditMode::Delete ? std::basic_string_view<CharT>{} : replacement;

    if (const char* error = validate_ranges(input, plan.edits); error != nullptr) {
        plan.edits.clear();
        plan.error = error;
        return plan;
    }

    if (!compute_stats(input, plan.edits, effective_replacement, plan.stats)) {
        plan.edits.clear();
        plan.error = "edit plan size overflow";
        return plan;
    }

    return plan;
}

template <typename CharT, typename Result>
[[nodiscard]] Result apply_impl(
    std::basic_string_view<CharT> input,
    const EditList& edits,
    std::basic_string_view<CharT> replacement) {
    Result result;

    if (const char* error = validate_ranges(input, edits); error != nullptr) {
        result.error = error;
        return result;
    }

    result.text.reserve(input.size());

    std::size_t cursor = 0;
    for (const auto& edit : edits) {
        result.text.append(input.substr(cursor, edit.start - cursor));
        result.text.append(replacement);
        cursor = edit.start + edit.length;
    }

    result.text.append(input.substr(cursor));
    return result;
}

}  // namespace

EditPlan plan_edits(
    std::string_view input,
    const MatchList& matches,
    std::string_view replacement,
    EditMode mode) {
    return plan_impl(input, matches, replacement, mode);
}

EditPlan plan_edits(
    std::u16string_view input,
    const MatchList& matches,
    std::u16string_view replacement,
    EditMode mode) {
    return plan_impl(input, matches, replacement, mode);
}

EditPlan plan_replace(
    std::string_view input,
    const MatchList& matches,
    std::string_view replacement) {
    return plan_edits(input, matches, replacement, EditMode::Replace);
}

EditPlan plan_replace(
    std::u16string_view input,
    const MatchList& matches,
    std::u16string_view replacement) {
    return plan_edits(input, matches, replacement, EditMode::Replace);
}

EditPlan plan_delete(std::string_view input, const MatchList& matches) {
    return plan_edits(input, matches, {}, EditMode::Delete);
}

EditPlan plan_delete(std::u16string_view input, const MatchList& matches) {
    return plan_edits(input, matches, {}, EditMode::Delete);
}

ApplyResult apply_edits(
    std::string_view input,
    const EditList& edits,
    std::string_view replacement) {
    return apply_impl<char, ApplyResult>(input, edits, replacement);
}

Apply16Result apply_edits(
    std::u16string_view input,
    const EditList& edits,
    std::u16string_view replacement) {
    return apply_impl<char16_t, Apply16Result>(input, edits, replacement);
}

}  // namespace rangeedit
