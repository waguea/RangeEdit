#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

namespace rangeedit {

struct Match {
    std::size_t start{};
    std::size_t length{};

    friend bool operator==(const Match&, const Match&) = default;
};

using MatchList = std::vector<Match>;

using ProgressCallback = std::function<void(std::size_t)>;

class CancellationToken;

enum class LiteralAlgorithm {
    Naive,
    Bmh,
    Kmp,
    StdSearch,
};

struct MatchOptions {
    bool case_sensitive = true;
    bool whole_word = false;
    std::size_t max_matches = std::numeric_limits<std::size_t>::max();
    CancellationToken* cancellation_token = nullptr;
    ProgressCallback progress_callback;
};

class CancellationToken {
public:
    void cancel() noexcept {
        cancelled_.store(true, std::memory_order_relaxed);
    }

    [[nodiscard]] bool cancelled() const noexcept {
        return cancelled_.load(std::memory_order_relaxed);
    }

    void reset() noexcept {
        cancelled_.store(false, std::memory_order_relaxed);
    }

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace rangeedit
