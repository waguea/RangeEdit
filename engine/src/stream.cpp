#include "rangeedit/stream.hpp"

#include "rangeedit/literal.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rangeedit {
namespace {

[[nodiscard]] std::size_t capped_search_limit(
    std::size_t max_matches,
    std::size_t matches_found,
    std::size_t extra) noexcept {
    constexpr auto kMax = std::numeric_limits<std::size_t>::max();
    if (max_matches == kMax) {
        return kMax;
    }

    const auto remaining = max_matches - matches_found;
    if (remaining > kMax - extra) {
        return kMax;
    }

    return remaining + extra;
}

}  // namespace

LiteralStreamScanner::LiteralStreamScanner(
    std::string needle,
    MatchOptions options,
    LiteralAlgorithm algorithm)
    : needle_(std::move(needle)),
      options_(options),
      algorithm_(algorithm) {
    if (!needle_.empty()) {
        carry_.reserve(needle_.size() + 1);
    }
}

MatchList LiteralStreamScanner::feed(std::string_view chunk) {
    if (needle_.empty() || options_.max_matches == 0 ||
        match_count_ >= options_.max_matches) {
        stream_offset_ += chunk.size();
        return {};
    }

    const auto chunk_start = stream_offset_;
    const auto stream_end = chunk_start + chunk.size();

    std::string buffer;
    buffer.reserve(carry_.size() + chunk.size());
    buffer.append(carry_);
    buffer.append(chunk);

    const auto buffer_start = chunk_start - carry_.size();

    auto search_options = options_;
    search_options.max_matches =
        capped_search_limit(options_.max_matches, match_count_, 2);

    const auto candidates = find_literal(buffer, needle_, search_options, algorithm_);

    MatchList accepted;
    for (const auto& match : candidates) {
        const auto global_start = buffer_start + match.start;
        const auto global_end = global_start + match.length;

        const bool already_processed = options_.whole_word
                                           ? global_end < chunk_start
                                           : global_end <= chunk_start;
        if (already_processed || global_start < accepted_end_) {
            continue;
        }

        if (options_.whole_word && global_end == stream_end) {
            continue;
        }

        accepted.push_back({global_start, match.length});
        accepted_end_ = global_end;
        ++match_count_;

        if (match_count_ >= options_.max_matches) {
            break;
        }
    }

    stream_offset_ = stream_end;

    const auto carry_size = std::min(needle_.size() + 1, buffer.size());
    if (carry_size == 0) {
        carry_.clear();
    } else {
        carry_.assign(buffer.data() + buffer.size() - carry_size, carry_size);
    }

    return accepted;
}

MatchList LiteralStreamScanner::finish() {
    if (needle_.empty() || !options_.whole_word || options_.max_matches == 0 ||
        match_count_ >= options_.max_matches || carry_.empty()) {
        return {};
    }

    const auto buffer_start = stream_offset_ - carry_.size();

    auto search_options = options_;
    search_options.max_matches =
        capped_search_limit(options_.max_matches, match_count_, 2);

    const auto candidates = find_literal(carry_, needle_, search_options, algorithm_);

    MatchList accepted;
    for (const auto& match : candidates) {
        const auto global_start = buffer_start + match.start;
        const auto global_end = global_start + match.length;

        if (global_end != stream_offset_ || global_start < accepted_end_) {
            continue;
        }

        accepted.push_back({global_start, match.length});
        accepted_end_ = global_end;
        ++match_count_;

        if (match_count_ >= options_.max_matches) {
            break;
        }
    }

    return accepted;
}

}  // namespace rangeedit
