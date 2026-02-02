// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <benchmark/benchmark.h>
#include <boost/unordered/unordered_flat_map.hpp>
#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

  // --- Enums for Data Types ---
  enum DataType {
    kRandom32 = 0,
    kHex32    = 1,
    kURL      = 2,
    kZipf     = 3 // New: Random content, Zipfian lengths (1-128)
  };

  // --- Generators ---
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
      for (int k = 0; k < 10; ++k) {
        s.push_back(static_cast<char>(char_dist(rng)));
      }
      s += suffixes[p_dist(rng)];
      result.push_back(std::move(s));
    }
    return result;
  }

  // New: Zipfian Length Generator
  // Generates strings with lengths 1..max_len following 1/k distribution.
  // Content is random lowercase alpha.
  auto generate_zipf_strings(std::size_t count, std::size_t max_len = 128)
    -> std::vector<std::string>
  {
    auto result = std::vector<std::string>{};
    result.reserve(count);

    // 1. Prepare Zipf weights: w[i] = 1/(i+1)
    // Using std::discrete_distribution which handles weight normalization.
    auto weights = std::vector<double>{};
    weights.reserve(max_len);
    for (std::size_t i = 1; i <= max_len; ++i) {
      weights.push_back(1.0 / static_cast<double>(i));
    }

    auto len_dist =
      std::discrete_distribution<std::size_t>(weights.begin(), weights.end());
    auto rng       = std::mt19937{std::random_device{}()};
    auto char_dist = std::uniform_int_distribution<int>{'a', 'z'};

    for (std::size_t i = 0; i < count; ++i) {
      // len_dist returns 0..(max_len-1). We want 1..max_len.
      std::size_t len = len_dist(rng) + 1;

      auto s = std::string{};
      s.reserve(len);
      for (std::size_t k = 0; k < len; ++k) {
        s.push_back(static_cast<char>(char_dist(rng)));
      }
      result.push_back(std::move(s));
    }
    return result;
  }

  auto generate_data(std::size_t count, int type) -> std::vector<std::string>
  {
    switch (type) {
    case kRandom32:
      return generate_random_strings(count, 32);
    case kHex32:
      return generate_hex_strings(count, 32);
    case kURL:
      return generate_urls(count);
    case kZipf:
      return generate_zipf_strings(count);
    default:
      return {};
    }
  }

  auto total_raw_size(const std::vector<std::string>& inputs) -> std::size_t
  {
    return std::accumulate(inputs.begin(),
      inputs.end(),
      std::size_t{0},
      [](std::size_t sum, const std::string& s) { return sum + s.size(); });
  }

} // namespace

// --- Benchmark: Construction (Map-Based) ---

template <template <typename,
  typename,
  typename,
  typename,
  typename...> typename MapType>
static void BM_Construction_Map(benchmark::State& state)
{
  auto const count     = static_cast<std::size_t>(state.range(0));
  auto const type      = static_cast<int>(state.range(1));
  auto const input     = generate_data(count, type);
  auto const raw_bytes = total_raw_size(input);

  for (auto _ : state) {
    std::vector<vault::algorithm::fsst_key> keys;
    keys.reserve(input.size());

    // Explicitly use default sample ratio (which is 1.0 / Level 9)
    auto dict = vault::algorithm::make_fsst_dictionary<MapType>(
      input, std::back_inserter(keys));

    benchmark::DoNotOptimize(dict);
    benchmark::DoNotOptimize(keys);

    auto total_compressed_size = static_cast<double>(dict.size_in_bytes())
      + static_cast<double>(keys.size() * sizeof(vault::algorithm::fsst_key));

    state.counters["Ratio"] =
      static_cast<double>(raw_bytes) / total_compressed_size;
  }
  state.SetItemsProcessed(state.iterations() * count);
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

// --- Benchmark: Construction (Legacy Sort-Based) ---

static void BM_Construction_Legacy(benchmark::State& state)
{
  auto const count     = static_cast<std::size_t>(state.range(0));
  auto const type      = static_cast<int>(state.range(1));
  auto const input     = generate_data(count, type);
  auto const raw_bytes = total_raw_size(input);

  for (auto _ : state) {
    auto [dict, keys] = vault::algorithm::fsst_dictionary::build(input);
    benchmark::DoNotOptimize(dict);
    benchmark::DoNotOptimize(keys);

    auto total_compressed_size = static_cast<double>(dict.size_in_bytes())
      + static_cast<double>(keys.size() * sizeof(vault::algorithm::fsst_key));

    state.counters["Ratio"] =
      static_cast<double>(raw_bytes) / total_compressed_size;
  }
  state.SetItemsProcessed(state.iterations() * count);
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

// --- Benchmark: Lookups ---

static void BM_Lookup_Sequential(benchmark::State& state)
{
  auto const count     = static_cast<std::size_t>(state.range(0));
  auto const type      = static_cast<int>(state.range(1));
  auto const input     = generate_data(count, type);
  auto const raw_bytes = total_raw_size(input);

  std::vector<vault::algorithm::fsst_key> keys;
  keys.reserve(input.size());
  auto dict = vault::algorithm::make_fsst_dictionary<boost::unordered_flat_map>(
    input, std::back_inserter(keys));

  for (auto _ : state) {
    for (const auto& key : keys) {
      auto s = dict[key];
      benchmark::DoNotOptimize(s);
    }
  }
  state.SetItemsProcessed(state.iterations() * keys.size());
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

static void BM_Lookup_RandomOrder(benchmark::State& state)
{
  auto const count     = static_cast<std::size_t>(state.range(0));
  auto const type      = static_cast<int>(state.range(1));
  auto const input     = generate_data(count, type);
  auto const raw_bytes = total_raw_size(input);

  std::vector<vault::algorithm::fsst_key> keys;
  keys.reserve(input.size());
  auto dict = vault::algorithm::make_fsst_dictionary<boost::unordered_flat_map>(
    input, std::back_inserter(keys));

  auto rng = std::mt19937{std::random_device{}()};
  std::shuffle(keys.begin(), keys.end(), rng);

  for (auto _ : state) {
    for (const auto& key : keys) {
      auto s = dict[key];
      benchmark::DoNotOptimize(s);
    }
  }
  state.SetItemsProcessed(state.iterations() * keys.size());
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

// --- Benchmark: Sampling Ratios ---

static void BM_SamplingRatio(benchmark::State& state)
{
  auto const denom     = static_cast<double>(state.range(0));
  auto const type      = static_cast<int>(state.range(1));
  auto const ratio_val = static_cast<float>(1.0 / denom);

  vault::algorithm::fsst_dictionary::sample_ratio ratio{ratio_val};

  std::size_t count     = 1'000'000;
  auto const  input     = generate_data(count, type);
  auto const  raw_bytes = total_raw_size(input);

  for (auto _ : state) {
    std::vector<vault::algorithm::fsst_key> keys;
    keys.reserve(input.size());

    auto dict =
      vault::algorithm::make_fsst_dictionary<boost::unordered_flat_map>(
        input, std::back_inserter(keys), {}, ratio);

    benchmark::DoNotOptimize(dict);
    benchmark::DoNotOptimize(keys);

    auto total_compressed_size = static_cast<double>(dict.size_in_bytes())
      + static_cast<double>(keys.size() * sizeof(vault::algorithm::fsst_key));

    state.counters["Ratio"] =
      static_cast<double>(raw_bytes) / total_compressed_size;
  }
  state.SetItemsProcessed(state.iterations() * count);
  state.SetBytesProcessed(state.iterations() * raw_bytes);
}

// --- Registration Logic ---

static void CustomArgs(benchmark::internal::Benchmark* b)
{
  std::vector<int64_t> counts = {10'000, 100'000, 1'000'000, 10'000'000};
  std::vector<int64_t> types  = {kRandom32, kHex32, kURL, kZipf}; // Added kZipf

  for (auto c : counts) {
    for (auto t : types) {
      b->Args({c, t});
    }
  }
}

static void SamplingArgs(benchmark::internal::Benchmark* b)
{
  std::vector<int64_t> types = {kRandom32, kHex32, kURL, kZipf}; // Added kZipf
  for (int64_t denom = 1; denom <= 8192; denom *= 2) {
    for (auto t : types) {
      b->Args({denom, t});
    }
  }
}

// 1. Legacy Sort-Based Construction
BENCHMARK(BM_Construction_Legacy)->Apply(CustomArgs);

// 2. Std Map Construction
BENCHMARK_TEMPLATE(BM_Construction_Map, std::unordered_map)->Apply(CustomArgs);

// 3. Boost Flat Map Construction
BENCHMARK_TEMPLATE(BM_Construction_Map, boost::unordered_flat_map)
  ->Apply(CustomArgs);

// 4. Lookup Performance
BENCHMARK(BM_Lookup_Sequential)->Apply(CustomArgs);
BENCHMARK(BM_Lookup_RandomOrder)->Apply(CustomArgs);

// 5. Sampling Ratios
BENCHMARK(BM_SamplingRatio)->Apply(SamplingArgs);

BENCHMARK_MAIN();
