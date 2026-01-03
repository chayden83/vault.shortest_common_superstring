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
// shortest_common_superstring/256    38510231 ns     38496430 ns           17
// shortest_common_superstring/512   210588417 ns    210581596 ns            3
// shortest_common_superstring/1024 1029923903 ns   1029466393 ns            1
// shortest_common_superstring/2048 3990672851 ns   3988920320 ns            1
// shortest_common_superstring/4096 1.4399e+10 ns   1.4383e+10 ns            1
// shortest_common_superstring/8192 4.1446e+10 ns   4.1427e+10 ns            1
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
  RangeMultiplier(2) ->
  Range(256, 8192);

// clang-format on
