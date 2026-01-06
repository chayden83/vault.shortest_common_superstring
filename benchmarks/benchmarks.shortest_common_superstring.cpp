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

// ----------------------------------------------------------------------------
// Benchmark                                  Time             CPU   Iterations
// ----------------------------------------------------------------------------
// shortest_common_superstring/256     18228456 ns     18224757 ns           38
// shortest_common_superstring/512     95512244 ns     95489932 ns            7
// shortest_common_superstring/1024   395013301 ns    394849611 ns            2
// shortest_common_superstring/2048  1551283540 ns   1550802734 ns            1
// shortest_common_superstring/4096  5021567731 ns   5019122442 ns            1
// shortest_common_superstring/8192  1.2800e+10 ns   1.2797e+10 ns            1
// shortest_common_superstring/10000 1.5460e+10 ns   1.5456e+10 ns            1
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
