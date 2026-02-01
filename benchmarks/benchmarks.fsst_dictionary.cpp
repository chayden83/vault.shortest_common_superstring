// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <benchmark/benchmark.h>
#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <numeric>
#include <random>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------------------------
// Benchmark                                    Time             CPU Iterations
// UserCounters...
// ----------------------------------------------------------------------------------------------
// BM_DictionaryBuild_Random/10000/16     8583156 ns      8581712 ns 72
// Ratio=0.796234 Savings=-0.255912 bytes_per_second=17.7806Mi/s
// items_per_second=1.16527M/s BM_DictionaryBuild_Random/10000/32     8784097 ns
// 8783706 ns           79 Ratio=0.997792 Savings=-2.2125m
// bytes_per_second=34.7434Mi/s items_per_second=1.13847M/s
// BM_DictionaryBuild_Random/10000/64    10554485 ns     10553685 ns 68
// Ratio=1.143 Savings=0.125109 bytes_per_second=57.833Mi/s
// items_per_second=947.536k/s BM_DictionaryBuild_Hex/10000/32        7703802 ns
// 7702676 ns           88 Ratio=1.28942 Savings=0.224459
// bytes_per_second=39.6194Mi/s items_per_second=1.29825M/s
// BM_DictionaryBuild_URLs/10000          8203324 ns      8199358 ns 84
// Ratio=1.75453 Savings=0.430045 bytes_per_second=55.4347Mi/s
// items_per_second=1.21961M/s BM_DictionaryBuild_Repeated/100000    63951450 ns
// 63942915 ns           10 items_per_second=1.56389M/s
// BM_DictionaryLookup_Random/10000/16     534975 ns       534896 ns 1304
// bytes_per_second=285.266Mi/s items_per_second=18.6952M/s
// BM_DictionaryLookup_Random/10000/64    1012327 ns      1012180 ns 723
// bytes_per_second=603.007Mi/s items_per_second=9.87966M/s
// BM_DictionaryLookup_URLs/10000          631243 ns       631134 ns 979
// items_per_second=15.8445M/s BM_DictionaryLookup_Large               138517 ns
// 138505 ns         4760 bytes_per_second=1.3771Gi/s
// items_per_second=721.998k/s
namespace {

  // --- Data Generators ---

  auto generate_random_strings(std::size_t count, std::size_t length)
    -> std::vector<std::string>
  {
    auto result = std::vector<std::string>{};
    result.reserve(count);

    auto engine = std::mt19937{std::random_device{}()};
    auto dist   = std::uniform_int_distribution<int>{'a', 'z'};

    for (auto i = std::size_t{0}; i < count; ++i) {
      auto s = std::string{};
      s.reserve(length);
      for (auto j = std::size_t{0}; j < length; ++j) {
        s.push_back(static_cast<char>(dist(engine)));
      }
      result.push_back(std::move(s));
    }
    return result;
  }

  auto generate_hex_strings(std::size_t count, std::size_t length)
    -> std::vector<std::string>
  {
    auto result = std::vector<std::string>{};
    result.reserve(count);

    auto chars = std::string_view{"0123456789ABCDEF"};
    auto rng   = std::mt19937{std::random_device{}()};
    auto dist  = std::uniform_int_distribution<std::size_t>(0, 15);

    for (auto i = std::size_t{0}; i < count; ++i) {
      auto s = std::string{};
      s.reserve(length);
      for (auto j = std::size_t{0}; j < length; ++j) {
        s.push_back(chars[dist(rng)]);
      }
      result.push_back(std::move(s));
    }
    return result;
  }

  auto generate_urls(std::size_t count) -> std::vector<std::string>
  {
    auto result = std::vector<std::string>{};
    result.reserve(count);

    auto prefixes = std::vector<std::string>{"https://www.google.com/search?q=",
      "https://api.github.com/users/",
      "http://example.com/item/"};
    auto suffixes =
      std::vector<std::string>{"&sourceid=chrome", "?v=4", "/details"};

    auto rng       = std::mt19937{std::random_device{}()};
    auto p_dist    = std::uniform_int_distribution<std::size_t>(0, 2);
    auto char_dist = std::uniform_int_distribution<int>{'a', 'z'};

    for (auto i = std::size_t{0}; i < count; ++i) {
      auto s = prefixes[p_dist(rng)];
      // Add some random "ID" in the middle
      for (int k = 0; k < 10; ++k) {
        s.push_back(static_cast<char>(char_dist(rng)));
      }
      s += suffixes[p_dist(rng)];
      result.push_back(std::move(s));
    }
    return result;
  }

  auto generate_repeating_strings(std::size_t count, std::size_t unique_count)
    -> std::vector<std::string>
  {
    auto pool   = generate_random_strings(unique_count, 32);
    auto result = std::vector<std::string>{};
    result.reserve(count);

    auto rng  = std::mt19937{std::random_device{}()};
    auto dist = std::uniform_int_distribution<std::size_t>(0, unique_count - 1);

    for (auto i = std::size_t{0}; i < count; ++i) {
      result.push_back(pool[dist(rng)]);
    }
    return result;
  }

  auto total_raw_size(const std::vector<std::string>& inputs) -> std::size_t
  {
    return std::accumulate(inputs.begin(),
      inputs.end(),
      std::size_t{0},
      [](std::size_t sum, const std::string& s) { return sum + s.size(); });
  }

} // namespace

// --- Benchmark: Construction (Build) with Compression Stats ---

static void BM_DictionaryBuild_Random(benchmark::State& state)
{
  auto const length    = static_cast<std::size_t>(state.range(1));
  auto const input     = generate_random_strings(state.range(0), length);
  auto const raw_bytes = total_raw_size(input);

  for (auto _ : state) {
    auto [dict, keys] = vault::algorithm::fsst_dictionary::build(input);
    benchmark::DoNotOptimize(dict);
    benchmark::DoNotOptimize(keys);

    auto total_compressed_size = static_cast<double>(dict.size_in_bytes())
      + static_cast<double>(keys.size() * sizeof(vault::algorithm::fsst_key));

    // Ratio > 1.0 means compression (e.g., 2.0 = 50% size)
    state.counters["Ratio"] =
      static_cast<double>(raw_bytes) / total_compressed_size;
    state.counters["Savings"] =
      1.0 - (total_compressed_size / static_cast<double>(raw_bytes));
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

static void BM_DictionaryBuild_Hex(benchmark::State& state)
{
  auto const length    = static_cast<std::size_t>(state.range(1));
  auto const input     = generate_hex_strings(state.range(0), length);
  auto const raw_bytes = total_raw_size(input);

  for (auto _ : state) {
    auto [dict, keys] = vault::algorithm::fsst_dictionary::build(input);
    benchmark::DoNotOptimize(dict);
    benchmark::DoNotOptimize(keys);

    auto total_compressed_size = static_cast<double>(dict.size_in_bytes())
      + static_cast<double>(keys.size() * sizeof(vault::algorithm::fsst_key));

    state.counters["Ratio"] =
      static_cast<double>(raw_bytes) / total_compressed_size;
    state.counters["Savings"] =
      1.0 - (total_compressed_size / static_cast<double>(raw_bytes));
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

static void BM_DictionaryBuild_URLs(benchmark::State& state)
{
  auto const input     = generate_urls(state.range(0));
  auto const raw_bytes = total_raw_size(input);

  for (auto _ : state) {
    auto [dict, keys] = vault::algorithm::fsst_dictionary::build(input);
    benchmark::DoNotOptimize(dict);
    benchmark::DoNotOptimize(keys);

    auto total_compressed_size = static_cast<double>(dict.size_in_bytes())
      + static_cast<double>(keys.size() * sizeof(vault::algorithm::fsst_key));

    state.counters["Ratio"] =
      static_cast<double>(raw_bytes) / total_compressed_size;
    state.counters["Savings"] =
      1.0 - (total_compressed_size / static_cast<double>(raw_bytes));
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

static void BM_DictionaryBuild_Repeated(benchmark::State& state)
{
  // High repetition: 10% unique
  auto const total  = static_cast<std::size_t>(state.range(0));
  auto const unique = std::max(std::size_t{1}, total / 10);
  auto const input  = generate_repeating_strings(total, unique);

  for (auto _ : state) {
    auto [dict, keys] = vault::algorithm::fsst_dictionary::build(input);
    benchmark::DoNotOptimize(dict);
    benchmark::DoNotOptimize(keys);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

// --- Benchmark: Lookup (Decompression) ---

static void BM_DictionaryLookup_Random(benchmark::State& state)
{
  auto const  length    = static_cast<std::size_t>(state.range(1));
  auto const  input     = generate_random_strings(state.range(0), length);
  auto const  build_res = vault::algorithm::fsst_dictionary::build(input);
  auto const& dict      = build_res.first;
  auto const& keys      = build_res.second;

  for (auto _ : state) {
    for (const auto& key : keys) {
      auto s = dict[key];
      benchmark::DoNotOptimize(s);
    }
  }
  state.SetItemsProcessed(state.iterations() * keys.size());
  state.SetBytesProcessed(state.iterations() * keys.size() * length);
}

static void BM_DictionaryLookup_URLs(benchmark::State& state)
{
  auto const  input     = generate_urls(state.range(0));
  auto const  build_res = vault::algorithm::fsst_dictionary::build(input);
  auto const& dict      = build_res.first;
  auto const& keys      = build_res.second;

  for (auto _ : state) {
    for (const auto& key : keys) {
      auto s = dict[key];
      benchmark::DoNotOptimize(s);
    }
  }
  state.SetItemsProcessed(state.iterations() * keys.size());
}

static void BM_DictionaryLookup_Large(benchmark::State& state)
{
  // Test 2KB strings to stress the reallocation logic in operator[]
  auto const  input     = generate_random_strings(100, 2048);
  auto const  build_res = vault::algorithm::fsst_dictionary::build(input);
  auto const& dict      = build_res.first;
  auto const& keys      = build_res.second;

  for (auto _ : state) {
    for (const auto& key : keys) {
      auto s = dict[key];
      benchmark::DoNotOptimize(s);
    }
  }
  state.SetItemsProcessed(state.iterations() * keys.size());
  state.SetBytesProcessed(state.iterations() * keys.size() * 2048);
}

// --- Registration ---

// Build: 10k strings, lengths 16, 32, 64
BENCHMARK(BM_DictionaryBuild_Random)
  ->Args({10'000, 16})
  ->Args({10'000, 32})
  ->Args({10'000, 64});

// Build Hex: 10k strings, length 32 (Simulate your UUID use case)
BENCHMARK(BM_DictionaryBuild_Hex)->Args({10'000, 32});

// Build URLs: 10k items
BENCHMARK(BM_DictionaryBuild_URLs)->Arg(10'000);

// Build Repeated: 100k items (Tests deduplication speed)
BENCHMARK(BM_DictionaryBuild_Repeated)->Arg(100'000);

// Lookup: 10k strings, lengths 16, 64
BENCHMARK(BM_DictionaryLookup_Random)->Args({10'000, 16})->Args({10'000, 64});

// Lookup URLs
BENCHMARK(BM_DictionaryLookup_URLs)->Arg(10'000);

// Lookup Large strings (2KB)
BENCHMARK(BM_DictionaryLookup_Large);

BENCHMARK_MAIN();
