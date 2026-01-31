// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <range/v3/view/take_exactly.hpp>
#include <range/v3/view/transform.hpp>

#include <range/v3/range/conversion.hpp>

#include <vault/algorithm/internal.hpp>
#include <vault/algorithm/shortest_common_superstring.hpp>

// ----------------------------------------------------------------------------
// Benchmark                                  Time             CPU   Iterations
// ----------------------------------------------------------------------------
// shortest_common_superstring/256      3533515 ns      3532753 ns          198
// shortest_common_superstring/512     13044572 ns     13036340 ns           53
// shortest_common_superstring/1024    48283333 ns     48250833 ns           13
// shortest_common_superstring/2048   177116306 ns    177067181 ns            4
// shortest_common_superstring/4096   598450524 ns    598333673 ns            1
// shortest_common_superstring/8192  1717887426 ns   1717194548 ns            1
// shortest_common_superstring/10000 2177187373 ns   2176746639 ns            1
void shortest_common_superstring(benchmark::State& state)
{
  auto strings = vault::internal::random_words_10k()
    | ::ranges::views::transform([](auto arg) { return std::string{arg}; })
    | ::ranges::views::take_exactly(state.range(0))
    | ::ranges::to<std::vector>();

  using superstring_bounds_t =
    vault::algorithm::greedy_shortest_common_superstring_fn::
      superstring_bounds_t<std::vector<std::string>>;

  auto out = std::vector<superstring_bounds_t>(10000);

  for (auto _ : state) {
    benchmark::DoNotOptimize(
      vault::algorithm::shortest_common_superstring(strings, out.begin()));
  }
}

BENCHMARK(shortest_common_superstring)->RangeMultiplier(2)->Range(256, 10000);
