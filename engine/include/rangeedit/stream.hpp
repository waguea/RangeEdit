#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "rangeedit/types.hpp"

namespace rangeedit {

class LiteralStreamScanner {
public:
    explicit LiteralStreamScanner(
        std::string needle,
        MatchOptions options = {},
        LiteralAlgorithm algorithm = LiteralAlgorithm::Bmh);

    [[nodiscard]] MatchList feed(std::string_view chunk);

    [[nodiscard]] MatchList finish();

    [[nodiscard]] std::size_t bytes_fed() const noexcept {
        return stream_offset_;
    }

    [[nodiscard]] std::size_t matches_found() const noexcept {
        return match_count_;
    }

private:
    [[nodiscard]] MatchList search_tail(const std::string& buffer, std::size_t buffer_start);

    std::string needle_;
    MatchOptions options_;
    LiteralAlgorithm algorithm_;
    std::string carry_;
    std::size_t stream_offset_ = 0;
    std::size_t accepted_end_ = 0;
    std::size_t match_count_ = 0;
};

}  // namespace rangeedit
