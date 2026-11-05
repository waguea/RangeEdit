#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "rangeedit/types.hpp"

namespace rangeedit {

enum class EditMode {
    Replace,
    Delete,
};

struct EditRange {
    std::size_t start{};
    std::size_t length{};

    friend bool operator==(const EditRange&, const EditRange&) = default;
};

using EditList = std::vector<EditRange>;

struct EditPlanStats {
    std::size_t edit_count{};
    std::size_t removed_units{};
    std::size_t inserted_units{};
    std::size_t output_units{};
};

struct EditPlan {
    EditList edits;
    EditPlanStats stats;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

template <typename CharT>
struct BasicApplyResult {
    std::basic_string<CharT> text;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

using ApplyResult = BasicApplyResult<char>;
using Apply16Result = BasicApplyResult<char16_t>;

[[nodiscard]] EditPlan plan_edits(
    std::string_view input,
    const MatchList& matches,
    std::string_view replacement,
    EditMode mode = EditMode::Replace);

[[nodiscard]] EditPlan plan_edits(
    std::u16string_view input,
    const MatchList& matches,
    std::u16string_view replacement,
    EditMode mode = EditMode::Replace);

[[nodiscard]] EditPlan plan_replace(
    std::string_view input,
    const MatchList& matches,
    std::string_view replacement);

[[nodiscard]] EditPlan plan_replace(
    std::u16string_view input,
    const MatchList& matches,
    std::u16string_view replacement);

[[nodiscard]] EditPlan plan_delete(std::string_view input, const MatchList& matches);

[[nodiscard]] EditPlan plan_delete(std::u16string_view input, const MatchList& matches);

[[nodiscard]] ApplyResult apply_edits(
    std::string_view input,
    const EditList& edits,
    std::string_view replacement);

[[nodiscard]] Apply16Result apply_edits(
    std::u16string_view input,
    const EditList& edits,
    std::u16string_view replacement);

}  // namespace rangeedit
