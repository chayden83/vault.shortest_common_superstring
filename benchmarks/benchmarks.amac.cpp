// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <benchmark/benchmark.h>

#include <iostream>

#include <random>
#include <vector>
#include <cassert>
#include <algorithm>
#include <functional>

#include <range/v3/action/sort.hpp>

#include <vault/algorithm/amac.hpp>

// clang-format off

static auto const randoms = [](std::size_t N) {
  auto result = std::vector<uint32_t>(N);
  auto engine = std::mt19937 { 0 };

  std::ranges::generate(result, std::bind_front(
    std::uniform_int_distribution<uint32_t> { 0u, 0xFFFFFFFFu }, engine
  ));

  return result;
};

static auto const needles = randoms(64);

static auto const haystack =
  ::ranges::actions::sort(randoms(25000000));;

void baseline(benchmark::State &state) {
  for(auto _ : state) {
    for(auto needle : needles) {
      assert(*std::ranges::lower_bound(haystack, needle) == needle);
    }
  }
}

template<uint8_t N>
void amac_binary_search(benchmark::State &state) {
  for(auto _ : state) {
    vault::algorithm::amac<N>(haystack, needles, [&](auto &&job) {
      assert(*job.needle_itr == *job.haystack_first);
    });
  }
}

BENCHMARK(baseline);

BENCHMARK(amac_binary_search< 1>);
BENCHMARK(amac_binary_search< 2>);
BENCHMARK(amac_binary_search< 4>);
BENCHMARK(amac_binary_search< 8>);
BENCHMARK(amac_binary_search<16>);
BENCHMARK(amac_binary_search<32>);
BENCHMARK(amac_binary_search<64>);

// clang-format on
