// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <chrono>
#include <ranges>
#include <string_view>
#include <vector>

#include <benchmark/benchmark.h>

#include <vault/algorithm/internal.hpp>
#include <vault/algorithm/histogram_algorithm.hpp>

// clang-format off

// Dimensions along which benchmarks should vary.
//
// - Histogram Implementation.
//   - Direct vs. Indirect
//   - Unordered
//     - Hash function
//     - Open vs. Closed Addressing
//     - Fill Factor
//   - Ordered
//     - Flat
//     - Tree
//   - Memory Allocator
// - Input Elements
//   - Type
//   - Count
// - Output Element Type

template<typename T>
struct output_element {
  using type = T;
};

template<>
struct output_element<std::string> {
  using type = std::string_view;
};

using output_element_t = typename output_element
  <std::ranges::range_value_t<decltype(vlt::internal::democracy_and_education())>>::type;

static auto time(auto invocable) {
  auto start = std::chrono::high_resolution_clock::now();

  invocable();

  return std::chrono::duration_cast<std::chrono::duration<double>>
    (std::chrono::high_resolution_clock::now() - start).count();
}

static void string_histogram_intersection(benchmark::State &state) {
  for(auto _ : state) {
    auto out = std::vector { static_cast<std::size_t>(state.range(0)), output_element_t {}};

    auto democracy_in_america    = vlt::internal::democracy_in_america   ().subspan(0, state.range(0));
    auto democracy_and_education = vlt::internal::democracy_and_education().subspan(0, state.range(0));

    state.SetIterationTime(time([&] {
      vlt::algorithm::histogram_intersection
	(democracy_in_america, democracy_and_education, out.begin());
    }));
  }
}

static void string_view_histogram_intersection(benchmark::State &state) {
  for(auto _ : state) {
    auto out = std::vector { static_cast<std::size_t>(state.range(0)), output_element_t {}};

    auto democracy_in_america    = vlt::internal::democracy_in_america   ().subspan(0, state.range(0));
    auto democracy_and_education = vlt::internal::democracy_and_education().subspan(0, state.range(0));

    state.SetIterationTime(time([&] {
      vlt::algorithm::histogram_intersection
	(democracy_in_america, democracy_and_education, out.begin());
    }));
  }
}

static void input_word_count(benchmark::internal::Benchmark* b) {
  b->Arg(8)->Arg(16)->Arg(32)->Arg(64)->Arg(512)->Arg(4096)->Arg(32768)->Arg(262144);
}

BENCHMARK(     string_histogram_intersection)->Apply(input_word_count)->UseManualTime();
BENCHMARK(string_view_histogram_intersection)->Apply(input_word_count)->UseManualTime();

// clang-format on
