// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/all.hpp>
#include <range/v3/view/take_exactly.hpp>
#include <range/v3/view/transform.hpp>

#include <vault/algorithm/internal.hpp>
#include <vault/algorithm/shortest_common_superstring.hpp>

// ----------------------------------------------------------------------------
// Benchmark                                  Time             CPU   Iterations
// ----------------------------------------------------------------------------
// shortest_common_superstring/256      3369746 ns      3367982 ns          197
// shortest_common_superstring/512     12393130 ns     12385308 ns           57
// shortest_common_superstring/1024    46548418 ns     46525554 ns           14
// shortest_common_superstring/2048   177708244 ns    177584010 ns            4
// shortest_common_superstring/4096   595216797 ns    594993092 ns            1
// shortest_common_superstring/8192  1670101970 ns   1669322299 ns            1
// shortest_common_superstring/10000 2090818838 ns   2089879336 ns            1
// bm_baseline_variable/256             3448477 ns      3446546 ns          208
// bm_baseline_variable/512            12268592 ns     12263878 ns           56
// bm_baseline_variable/1024           45284103 ns     45264125 ns           15
// bm_baseline_variable/2048          168452152 ns    168365220 ns            4
// bm_baseline_variable/4096          565352144 ns    565039165 ns            1
// bm_baseline_fixed_32/256            13448832 ns     13440756 ns           52
// bm_baseline_fixed_32/512            53553871 ns     53528132 ns           12
// bm_baseline_fixed_32/1024          214048515 ns    213934089 ns            3
// bm_baseline_fixed_32/2048          880580484 ns    879797009 ns            1
// bm_baseline_fixed_32/4096         3599334293 ns   3597410944 ns            1
// bm_comparator_variable/256           4259135 ns      4256600 ns          164
// bm_comparator_variable/512          15361298 ns     15355755 ns           44
// bm_comparator_variable/1024         58096158 ns     58065320 ns           12
// bm_comparator_variable/2048        207115415 ns    207004177 ns            3
// bm_comparator_variable/4096        700386781 ns    700051642 ns            1
// bm_comparator_fixed_32/256          17982853 ns     17972894 ns           39
// bm_comparator_fixed_32/512          71917744 ns     71891747 ns            9
// bm_comparator_fixed_32/1024        292180533 ns    292076684 ns            2
// bm_comparator_fixed_32/2048       1218495340 ns   1217822015 ns            1
// bm_comparator_fixed_32/4096       4787336945 ns   4785199484 ns            1
// bm_projection_widgets/256            3751052 ns      3749343 ns          186
// bm_projection_widgets/512           13653506 ns     13647956 ns           50
// bm_projection_widgets/1024          50445579 ns     50416311 ns           13
// bm_projection_widgets/2048         184214750 ns    184088771 ns            4
// bm_projection_widgets/4096         618633674 ns    618373518 ns            1
// bm_int_vectors_variable/256          3730228 ns      3727580 ns          190
// bm_int_vectors_variable/512         13928299 ns     13919812 ns           51
// bm_int_vectors_variable/1024        50125319 ns     50111951 ns           13
// bm_int_vectors_variable/2048       181820103 ns    181702158 ns            4
// bm_int_vectors_variable/4096       634339294 ns    633998562 ns            1
namespace {

  using namespace std::literals::string_literals;

  // --- Helpers & Generators ---

  // Generates N random strings of fixed length L
  auto generate_fixed_strings(std::size_t count, std::size_t length)
    -> std::vector<std::string>
  {
    auto rng  = std::mt19937{std::random_device{}()};
    auto dist = std::uniform_int_distribution<char>{'a', 'z'};

    auto result = std::vector<std::string>(count);
    for (auto& s : result) {
      s.resize(length);
      for (auto& c : s) {
        c = dist(rng);
      }
    }
    return result;
  }

  // Fetches a subset of the variable-length dictionary
  auto get_variable_strings(std::size_t count) -> std::vector<std::string>
  {
    return vault::internal::random_words_10k()
      | ::ranges::views::transform([](auto arg) { return std::string{arg}; })
      | ::ranges::views::take_exactly(count) | ::ranges::to<std::vector>();
  }

  // --- Custom Types & Comparators ---

  struct fast_case_insensitive_eq {
    [[nodiscard]]
    auto operator()(char a, char b) const -> bool
    {
      auto const la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a + 32) : a;
      auto const lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b + 32) : b;
      return la == lb;
    }
  };

  struct widget {
    int         id;
    std::string payload;
  };

} // namespace

// ----------------------------------------------------------------------------
// User's Original Benchmark
// ----------------------------------------------------------------------------

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

// ----------------------------------------------------------------------------
// New Extended Benchmarks
// ----------------------------------------------------------------------------

void bm_baseline_variable(benchmark::State& state)
{
  auto const count   = static_cast<std::size_t>(state.range(0));
  auto       strings = get_variable_strings(count);

  using bounds_t = vault::algorithm::greedy_shortest_common_superstring_fn::
    superstring_bounds_t<decltype(strings)>;

  auto out = std::vector<bounds_t>(count);

  for (auto _ : state) {
    benchmark::DoNotOptimize(
      vault::algorithm::shortest_common_superstring(strings, out.begin()));
  }
}

void bm_baseline_fixed_32(benchmark::State& state)
{
  auto const count   = static_cast<std::size_t>(state.range(0));
  auto       strings = generate_fixed_strings(count, 32);

  using bounds_t = vault::algorithm::greedy_shortest_common_superstring_fn::
    superstring_bounds_t<decltype(strings)>;

  auto out = std::vector<bounds_t>(count);

  for (auto _ : state) {
    benchmark::DoNotOptimize(
      vault::algorithm::shortest_common_superstring(strings, out.begin()));
  }
}

void bm_comparator_variable(benchmark::State& state)
{
  auto const count   = static_cast<std::size_t>(state.range(0));
  auto       strings = get_variable_strings(count);

  using bounds_t = vault::algorithm::greedy_shortest_common_superstring_fn::
    superstring_bounds_t<decltype(strings)>;

  auto out  = std::vector<bounds_t>(count);
  auto comp = fast_case_insensitive_eq{};

  for (auto _ : state) {
    benchmark::DoNotOptimize(vault::algorithm::shortest_common_superstring(
      strings, out.begin(), comp));
  }
}

void bm_comparator_fixed_32(benchmark::State& state)
{
  auto const count   = static_cast<std::size_t>(state.range(0));
  auto       strings = generate_fixed_strings(count, 32);

  using bounds_t = vault::algorithm::greedy_shortest_common_superstring_fn::
    superstring_bounds_t<decltype(strings)>;

  auto out  = std::vector<bounds_t>(count);
  auto comp = fast_case_insensitive_eq{};

  for (auto _ : state) {
    benchmark::DoNotOptimize(vault::algorithm::shortest_common_superstring(
      strings, out.begin(), comp));
  }
}

void bm_projection_widgets(benchmark::State& state)
{
  auto const count = static_cast<std::size_t>(state.range(0));

  // Correctly construct a vector of vectors (sequences) of widgets.
  // We base this on the random string data to ensure overlap characteristics
  // are preserved.
  auto raw_strings = get_variable_strings(count);

  auto widgets_sequences = raw_strings
    | ::ranges::views::transform([](const std::string& s) {
        return s | ::ranges::views::transform([](char c) {
          return widget{static_cast<int>(c), "payload"s};
        }) | ::ranges::to<std::vector<widget>>();
      })
    | ::ranges::to<std::vector<std::vector<widget>>>();

  auto proj = [](const widget& w) -> int { return w.id; };

  using bounds_t = vault::algorithm::greedy_shortest_common_superstring_fn::
    superstring_bounds_t<decltype(widgets_sequences), decltype(proj)>;

  auto out = std::vector<bounds_t>(count);

  for (auto _ : state) {
    benchmark::DoNotOptimize(vault::algorithm::shortest_common_superstring(
      widgets_sequences, out.begin(), proj));
  }
}

void bm_int_vectors_variable(benchmark::State& state)
{
  auto const count = static_cast<std::size_t>(state.range(0));

  // Fix pipe error by capturing input strings first
  auto raw_strings = get_variable_strings(count);

  auto input = raw_strings
    | ::ranges::views::transform([](const std::string& s) {
        return s | ::ranges::views::transform([](char c) {
          return static_cast<int>(c);
        }) | ::ranges::to<std::vector<int>>();
      })
    | ::ranges::to<std::vector<std::vector<int>>>();

  using bounds_t = vault::algorithm::greedy_shortest_common_superstring_fn::
    superstring_bounds_t<decltype(input)>;

  auto out = std::vector<bounds_t>(count);

  for (auto _ : state) {
    benchmark::DoNotOptimize(
      vault::algorithm::shortest_common_superstring(input, out.begin()));
  }
}

// Register Benchmarks
BENCHMARK(shortest_common_superstring)->RangeMultiplier(2)->Range(256, 10000);
BENCHMARK(bm_baseline_variable)->RangeMultiplier(2)->Range(256, 4096);
BENCHMARK(bm_baseline_fixed_32)->RangeMultiplier(2)->Range(256, 4096);
BENCHMARK(bm_comparator_variable)->RangeMultiplier(2)->Range(256, 4096);
BENCHMARK(bm_comparator_fixed_32)->RangeMultiplier(2)->Range(256, 4096);
BENCHMARK(bm_projection_widgets)->RangeMultiplier(2)->Range(256, 4096);
BENCHMARK(bm_int_vectors_variable)->RangeMultiplier(2)->Range(256, 4096);
