// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

#include <benchmark/benchmark.h>

#include <range/v3/view/transform.hpp>
#include <range/v3/view/take_exactly.hpp>

#include <range/v3/range/conversion.hpp>

#include <vault/algorithm/internal.hpp>
#include <vault/algorithm/shortest_common_superstring.hpp>

// clang-format off

// ---------------------------------------------------------------------------
// Benchmark                                 Time             CPU   Iterations
// ---------------------------------------------------------------------------
// shortest_common_superstring/256    25514193 ns     25511597 ns           28
// shortest_common_superstring/512   141118375 ns    141111659 ns            5
// shortest_common_superstring/1024  638963581 ns    638889634 ns            1
// shortest_common_superstring/2048 2386243683 ns   2386024710 ns            1
// shortest_common_superstring/4096 7756590176 ns   7755799602 ns            1
// shortest_common_superstring/8192 2.0386e+10 ns   2.0384e+10 ns            1
void shortest_common_superstring(benchmark::State &state) {
  auto strings = vault::internal::random_words_10k()
    | ::ranges::views::transform([](auto arg) { return std::string { arg }; })
    | ::ranges::views::take_exactly(state.range(0))
    | ::ranges::to<std::vector>();

  auto out = std::vector<std::pair<std::ptrdiff_t, std::size_t>>(10000);

  for(auto _ : state) {
    benchmark::DoNotOptimize
      (vault::algorithm::shortest_common_superstring(strings, out.begin()));
  }
}

BENCHMARK(shortest_common_superstring) ->
  RangeMultiplier(2) -> Range(256, 10000);

// clang-format on
