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
// BM_DictionaryBuild_Random/10000/16     8830975 ns      8828653 ns 75
// Ratio=0.56976 Savings=-0.755125 bytes_per_second=17.2833Mi/s
// items_per_second=1.13268M/s BM_DictionaryBuild_Random/10000/32     8757354 ns
// 8756713 ns           80 Ratio=0.800244 Savings=-0.249619
// bytes_per_second=34.8505Mi/s items_per_second=1.14198M/s
// BM_DictionaryBuild_Random/10000/64    10508673 ns     10507656 ns 67
// Ratio=0.99982 Savings=-179.687u bytes_per_second=58.0864Mi/s
// items_per_second=951.687k/s BM_DictionaryBuild_Hex/10000/32        7566898 ns
// 7565894 ns           91 Ratio=0.975152 Savings=-0.0254812
// bytes_per_second=40.3357Mi/s items_per_second=1.32172M/s
// BM_DictionaryBuild_URLs/10000          8070748 ns      8070454 ns 88
// Ratio=1.37193 Savings=0.271101 bytes_per_second=56.3329Mi/s
// items_per_second=1.23909M/s BM_DictionaryBuild_Repeated/100000    62886795 ns
// 62878257 ns           10 items_per_second=1.59037M/s
// BM_DictionaryLookup_Random/10000/16     521589 ns       521557 ns 1216
// bytes_per_second=292.562Mi/s items_per_second=19.1733M/s
// BM_DictionaryLookup_Random/10000/64     902705 ns       902638 ns 780
// bytes_per_second=676.186Mi/s items_per_second=11.0786M/s
// BM_DictionaryLookup_URLs/10000          595950 ns       595908 ns 1136
// items_per_second=16.7811M/s BM_DictionaryLookup_Large               142900 ns
// 142840 ns         4845 bytes_per_second=1.33531Gi/s
// items_per_second=700.086k/s
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
