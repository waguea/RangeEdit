#include "rangeedit/literal.hpp"

#ifdef RANGEEDIT_HAS_REGEX
#include "rangeedit/regex.hpp"
#endif

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace {

std::string build_corpus(std::size_t size) {
    std::string text(size, ' ');

    std::mt19937 generator(0x5EEDU);
    std::uniform_int_distribution<int> distribution(0, 25);
    for (std::size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>('a' + distribution(generator));
    }

    const std::string sample = "needle";
    for (std::size_t position = 1000; position + sample.size() < size; position += 4096) {
        text.replace(position, sample.size(), sample);
    }

    return text;
}

void run_find(benchmark::State& state, rangeedit::LiteralAlgorithm algorithm) {
    const auto size = static_cast<std::size_t>(state.range(0));
    const auto text = build_corpus(size);
    const std::string needle = "needle";
    const rangeedit::MatchOptions options;

    for (auto _ : state) {
        auto matches = rangeedit::find_literal(text, needle, options, algorithm);
        benchmark::DoNotOptimize(matches);
    }

    const auto bytes = static_cast<std::int64_t>(text.size());
    state.SetBytesProcessed(state.iterations() * bytes);
}

void BM_FindNaive(benchmark::State& state) {
    run_find(state, rangeedit::LiteralAlgorithm::Naive);
}

void BM_FindBmh(benchmark::State& state) {
    run_find(state, rangeedit::LiteralAlgorithm::Bmh);
}

void BM_FindKmp(benchmark::State& state) {
    run_find(state, rangeedit::LiteralAlgorithm::Kmp);
}

void BM_FindStdSearch(benchmark::State& state) {
    run_find(state, rangeedit::LiteralAlgorithm::StdSearch);
}

#ifdef RANGEEDIT_HAS_REGEX
void run_regex(benchmark::State& state, std::string_view pattern) {
    const auto size = static_cast<std::size_t>(state.range(0));
    const auto text = build_corpus(size);
    const rangeedit::MatchOptions options;

    for (auto _ : state) {
        auto result = rangeedit::find_regex(text, pattern, options);
        if (!result.ok()) {
            state.SkipWithError(result.error.c_str());
            return;
        }
        benchmark::DoNotOptimize(result.matches);
    }

    const auto bytes = static_cast<std::int64_t>(text.size());
    state.SetBytesProcessed(state.iterations() * bytes);
}

void BM_FindRegexLiteral(benchmark::State& state) {
    run_regex(state, "needle");
}

void BM_FindRegexCharacterClass(benchmark::State& state) {
    run_regex(state, "n[a-z]{4}e");
}
#endif

}  // namespace

BENCHMARK(BM_FindNaive)->RangeMultiplier(16)->Range(1 << 20, 1 << 24);
BENCHMARK(BM_FindBmh)->RangeMultiplier(16)->Range(1 << 20, 1 << 24)->Args({100 * 1024 * 1024});
BENCHMARK(BM_FindKmp)->RangeMultiplier(16)->Range(1 << 20, 1 << 24);
BENCHMARK(BM_FindStdSearch)->RangeMultiplier(16)->Range(1 << 20, 1 << 24);

#ifdef RANGEEDIT_HAS_REGEX
BENCHMARK(BM_FindRegexLiteral)->RangeMultiplier(16)->Range(1 << 20, 1 << 24);
BENCHMARK(BM_FindRegexCharacterClass)->RangeMultiplier(16)->Range(1 << 20, 1 << 24);
#endif

BENCHMARK_MAIN();
