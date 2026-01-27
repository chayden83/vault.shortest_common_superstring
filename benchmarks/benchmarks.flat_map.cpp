#include <algorithm>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <benchmark/benchmark.h>

#include <vault/flat_map/aliases.hpp>

using namespace eytzinger;

// --- Configuration ---

// Define aliases for int-based maps
template <typename K, typename V>
using implicit_btree_map =
  layout_map<K, V, std::less<K>, eytzinger::implicit_btree_layout_policy<16>>;

// Helper to generate random ints
std::vector<int> generate_random_ints(size_t n)
{
  std::vector<int>                   data(n);
  std::mt19937                       rng(42);
  std::uniform_int_distribution<int> dist(
    std::numeric_limits<int>::min(), std::numeric_limits<int>::max());

  for (size_t i = 0; i < n; ++i) {
    data[i] = dist(rng);
  }
  return data;
}

// Helper to generate random strings
std::vector<std::string> generate_random_strings(size_t n, size_t len = 32)
{
  std::vector<std::string> data;
  data.reserve(n);
  std::mt19937      rng(42);
  static const char charset[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";
  std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);

  for (size_t i = 0; i < n; ++i) {
    std::string s;
    s.reserve(len);
    for (size_t j = 0; j < len; ++j) {
      s += charset[dist(rng)];
    }
    data.push_back(std::move(s));
  }
  return data;
}

// --- Arena Helper for StringViews ---
struct StringArena {
  std::vector<char>             buffer; // The "Arena": Contiguous string data
  std::vector<std::string_view> views;
};

// Generates strings, SORTS them, and packs them contiguously into an arena.
// This maximizes data locality for comparisons.
StringArena generate_sorted_string_arena(size_t n, size_t len = 32)
{
  auto strings = generate_random_strings(n, len);
  std::sort(strings.begin(), strings.end());

  StringArena arena;
  arena.buffer.reserve(n * len);
  arena.views.reserve(n);

  for (const auto& s : strings) {
    size_t offset = arena.buffer.size();
    arena.buffer.insert(arena.buffer.end(), s.begin(), s.end());
    arena.views.emplace_back(arena.buffer.data() + offset, s.size());
  }
  return arena;
}

// --- Benchmarks ---

/**
 * @brief Benchmark: Random Lookup
 */
template <template <typename...> class MapType, typename KeyT>
static void BM_Lookup_Random(benchmark::State& state)
{
  const size_t n = state.range(0);

  // Setup Data
  std::vector<KeyT> keys;
  if constexpr (std::is_same_v<KeyT, std::string>) {
    keys = generate_random_strings(n);
  } else {
    keys = generate_random_ints(n);
  }

  // Create map
  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(n);
  int val_counter = 0;
  for (const auto& k : keys) {
    pairs.emplace_back(k, val_counter++);
  }

  // MapType is expected to be a template taking <Key, Value>
  MapType<KeyT, int> map(pairs.begin(), pairs.end());

  // Setup Search Keys (shuffle)
  std::vector<KeyT> lookups = keys;
  std::mt19937      rng(123);
  std::shuffle(lookups.begin(), lookups.end(), rng);

  size_t idx = 0;

  for (auto _ : state) {
    const auto& key = lookups[idx];
    auto        it  = map.find(key);
    benchmark::DoNotOptimize(it);

    idx = (idx + 1);
    if (idx >= n) {
      idx = 0;
    }
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("O(log N)");
}

/**
 * @brief Benchmark: Lookup (StringView with Sorted Arena)
 * Tests if data locality restores layout performance for indirect types.
 */
template <template <typename...> class MapType>
static void BM_Lookup_StringView_Arena(benchmark::State& state)
{
  const size_t n = state.range(0);

  // 1. Generate Arena (Data is sorted and contiguous)
  StringArena arena = generate_sorted_string_arena(n);

  // 2. Create Map
  // Note: Since 'arena.views' is already sorted, the map construction
  // simply permutes them into layout order without re-sorting values.
  std::vector<std::pair<std::string_view, int>> pairs;
  pairs.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    pairs.emplace_back(arena.views[i], static_cast<int>(i));
  }

  MapType<std::string_view, int> map(pairs.begin(), pairs.end());

  // 3. Setup Lookups (Shuffle the views for random access pattern)
  std::vector<std::string_view> lookups = arena.views;
  std::mt19937                  rng(123);
  std::shuffle(lookups.begin(), lookups.end(), rng);

  size_t idx = 0;

  for (auto _ : state) {
    // We search using the view.
    // Ideally, Eytzinger prefetching should now pull in the *data* // because
    // the data for adjacent ranks is adjacent in the arena.
    const auto& key = lookups[idx];
    auto        it  = map.find(key);
    benchmark::DoNotOptimize(it);

    idx = (idx + 1);
    if (idx >= n) {
      idx = 0;
    }
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("Arena+View");
}

/**
 * @brief Benchmark: Iteration (Full Scan)
 */
template <template <typename...> class MapType, typename KeyT>
static void BM_Iteration(benchmark::State& state)
{
  const size_t n = state.range(0);

  std::vector<KeyT> keys;
  if constexpr (std::is_same_v<KeyT, std::string>) {
    keys = generate_random_strings(n);
  } else {
    keys = generate_random_ints(n);
  }

  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(n);
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0);
  }

  MapType<KeyT, int> map(pairs.begin(), pairs.end());

  for (auto _ : state) {
    for (auto it = map.begin(); it != map.end(); ++it) {
      benchmark::DoNotOptimize(*it);
    }
  }

  state.SetItemsProcessed(state.iterations() * n);
  state.SetLabel("O(N)");
}

/**
 * @brief Benchmark: Construction
 */
template <template <typename...> class MapType, typename KeyT>
static void BM_Construction(benchmark::State& state)
{
  const size_t n = state.range(0);

  std::vector<KeyT> keys;
  if constexpr (std::is_same_v<KeyT, std::string>) {
    keys = generate_random_strings(n);
  } else {
    keys = generate_random_ints(n);
  }

  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(n);
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0);
  }

  for (auto _ : state) {
    // Copy pairs to simulate fresh construction from unsorted input
    auto               local_pairs = pairs;
    MapType<KeyT, int> map(local_pairs.begin(), local_pairs.end());
    benchmark::DoNotOptimize(map);
  }

  state.SetItemsProcessed(state.iterations() * n);
  state.SetLabel("Sort + Permute");
}

// --- Register Benchmarks ---

// Range: 256 elements to 1 Million (Reduced max for strings to keep bench time
// sane)
#define BENCH_ARGS_INT                                                         \
  ->RangeMultiplier(4)->Range(256, 4 << 20)->Unit(benchmark::kNanosecond)

#define BENCH_ARGS_STR                                                         \
  ->RangeMultiplier(4)->Range(256, 1 << 18)->Unit(benchmark::kNanosecond)

// --- INTEGER BENCHMARKS ---

// 1. Sorted Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, sorted_map, int)
BENCH_ARGS_INT->Name("Int/Sorted/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, sorted_map, int)
BENCH_ARGS_INT->Name("Int/Sorted/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, sorted_map, int)
BENCH_ARGS_INT->Name("Int/Sorted/Construct");

// 2. Eytzinger Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, eytzinger_map, int)
BENCH_ARGS_INT->Name("Int/Eytzinger/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, eytzinger_map, int)
BENCH_ARGS_INT->Name("Int/Eytzinger/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, eytzinger_map, int)
BENCH_ARGS_INT->Name("Int/Eytzinger/Construct");

// 3. Implicit B-Tree Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, implicit_btree_map, int)
BENCH_ARGS_INT->Name("Int/BTree/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, implicit_btree_map, int)
BENCH_ARGS_INT->Name("Int/BTree/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, implicit_btree_map, int)
BENCH_ARGS_INT->Name("Int/BTree/Construct");

// --- STRING BENCHMARKS ---

// 1. Sorted Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, sorted_map, std::string)
BENCH_ARGS_STR->Name("Str/Sorted/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, sorted_map, std::string)
BENCH_ARGS_STR->Name("Str/Sorted/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, sorted_map, std::string)
BENCH_ARGS_STR->Name("Str/Sorted/Construct");

// 2. Eytzinger Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, eytzinger_map, std::string)
BENCH_ARGS_STR->Name("Str/Eytzinger/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, eytzinger_map, std::string)
BENCH_ARGS_STR->Name("Str/Eytzinger/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, eytzinger_map, std::string)
BENCH_ARGS_STR->Name("Str/Eytzinger/Construct");

// 3. Implicit B-Tree Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, btree_map, std::string)
BENCH_ARGS_STR->Name("Str/BTree/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, btree_map, std::string)
BENCH_ARGS_STR->Name("Str/BTree/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, btree_map, std::string)
BENCH_ARGS_STR->Name("Str/BTree/Construct");

// --- STRING VIEW ARENA BENCHMARKS (Optimized) ---

// 1. Sorted Map (View)
BENCHMARK_TEMPLATE(BM_Lookup_StringView_Arena, sorted_map)
BENCH_ARGS_STR->Name("View/Sorted/Lookup");

// 2. Eytzinger Map (View)
BENCHMARK_TEMPLATE(BM_Lookup_StringView_Arena, eytzinger_map)
BENCH_ARGS_STR->Name("View/Eytzinger/Lookup");

// 3. Implicit B-Tree Map (View)
BENCHMARK_TEMPLATE(BM_Lookup_StringView_Arena, btree_map)
BENCH_ARGS_STR->Name("View/BTree/Lookup");

template <typename T> std::vector<T> generate_random_data(size_t n)
{
  std::vector<T> data;
  data.reserve(n);
  std::mt19937_64 rng(42);

  if constexpr (std::is_integral_v<T>) {
    using DistType   = std::conditional_t<(sizeof(T) < 2), int16_t, T>;
    DistType min_val = static_cast<DistType>(std::numeric_limits<T>::min());
    DistType max_val = static_cast<DistType>(std::numeric_limits<T>::max());
    std::uniform_int_distribution<DistType> dist(min_val, max_val);
    for (size_t i = 0; i < n; ++i) {
      data.push_back(static_cast<T>(dist(rng)));
    }
  } else if constexpr (std::is_same_v<T, std::string>) {
    static const char charset[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    for (size_t i = 0; i < n; ++i) {
      std::string s;
      s.reserve(32);
      for (size_t j = 0; j < 32; ++j) {
        s += charset[dist(rng)];
      }
      data.push_back(std::move(s));
    }
  }
  return data;
}

// --- Benchmark Functions ---

// 1. Batch Lower Bound
template <typename LayoutPolicy, typename KeyT>
static void BM_Batch_Lower_Bound(benchmark::State& state)
{
  const size_t n           = state.range(0);
  const size_t num_needles = 2048;

  auto                              keys = generate_random_data<KeyT>(n);
  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(keys.size());
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0);
  }

  using MapType = layout_map<KeyT, int, std::less<KeyT>, LayoutPolicy>;
  MapType map(pairs.begin(), pairs.end());

  auto needles = generate_random_data<KeyT>(num_needles);

  // FIX: Use const_iterator for needles to match AMAC conductor behavior
  using NeedleIter = typename std::vector<KeyT>::const_iterator;
  using MapIter    = typename MapType::const_iterator;
  using ResultPair = std::pair<NeedleIter, MapIter>;

  std::vector<ResultPair> results;
  results.reserve(num_needles);

  for (auto _ : state) {
    results.clear();
    map.template batch_lower_bound<16>(needles, std::back_inserter(results));
    benchmark::DoNotOptimize(results.data());
  }
  state.SetItemsProcessed(state.iterations() * num_needles);
}

// 2. Batch Upper Bound
template <typename LayoutPolicy, typename KeyT>
static void BM_Batch_Upper_Bound(benchmark::State& state)
{
  const size_t n           = state.range(0);
  const size_t num_needles = 2048;

  auto                              keys = generate_random_data<KeyT>(n);
  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(keys.size());
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0);
  }

  using MapType = layout_map<KeyT, int, std::less<KeyT>, LayoutPolicy>;
  MapType map(pairs.begin(), pairs.end());

  auto needles = generate_random_data<KeyT>(num_needles);

  // FIX: Use const_iterator
  using NeedleIter = typename std::vector<KeyT>::const_iterator;
  using MapIter    = typename MapType::const_iterator;
  using ResultPair = std::pair<NeedleIter, MapIter>;
  std::vector<ResultPair> results;
  results.reserve(num_needles);

  for (auto _ : state) {
    results.clear();
    map.template batch_upper_bound<16>(needles, std::back_inserter(results));
    benchmark::DoNotOptimize(results.data());
  }
  state.SetItemsProcessed(state.iterations() * num_needles);
}

// 3. Batch Find
template <typename LayoutPolicy, typename KeyT>
static void BM_Batch_Find(benchmark::State& state)
{
  const size_t n           = state.range(0);
  const size_t num_needles = 2048;

  auto                              keys = generate_random_data<KeyT>(n);
  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(keys.size());
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0);
  }

  using MapType = layout_map<KeyT, int, std::less<KeyT>, LayoutPolicy>;
  MapType map(pairs.begin(), pairs.end());

  auto needles = generate_random_data<KeyT>(num_needles);

  // FIX: Use const_iterator
  using NeedleIter = typename std::vector<KeyT>::const_iterator;
  using MapIter    = typename MapType::const_iterator;
  using ResultPair = std::pair<NeedleIter, MapIter>;
  std::vector<ResultPair> results;
  results.reserve(num_needles);

  for (auto _ : state) {
    results.clear();
    map.template batch_find<16>(needles, std::back_inserter(results));
    benchmark::DoNotOptimize(results.data());
  }
  state.SetItemsProcessed(state.iterations() * num_needles);
}

// --- Registration Macros ---

#define BATCH_ARGS                                                             \
  ->RangeMultiplier(4)->Range(256, 1 << 20)->Unit(benchmark::kNanosecond)

#define REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, KeyName, KeyType)    \
  BENCHMARK_TEMPLATE(BM_Batch_Lower_Bound, LayoutType, KeyType)                \
  BATCH_ARGS->Name(LayoutName "/" KeyName "/BatchLB");                         \
  BENCHMARK_TEMPLATE(BM_Batch_Upper_Bound, LayoutType, KeyType)                \
  BATCH_ARGS->Name(LayoutName "/" KeyName "/BatchUB");                         \
  BENCHMARK_TEMPLATE(BM_Batch_Find, LayoutType, KeyType)                       \
  BATCH_ARGS->Name(LayoutName "/" KeyName "/BatchFind");

#define REGISTER_ALL_BATCH_TYPES(LayoutName, LayoutType)                       \
  REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, "int8", int8_t)            \
  REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, "uint8", uint8_t)          \
  REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, "int16", int16_t)          \
  REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, "uint16", uint16_t)        \
  REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, "uint32", uint32_t)        \
  REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, "int64", int64_t)          \
  REGISTER_BATCH_BENCHMARKS(LayoutName, LayoutType, "uint64", uint64_t)

// --- Register Batch Benchmarks ---

REGISTER_ALL_BATCH_TYPES("Sorted", eytzinger::sorted_layout_policy)
REGISTER_ALL_BATCH_TYPES("Eytzinger", eytzinger::eytzinger_layout_policy<6>)
REGISTER_ALL_BATCH_TYPES("BTree", eytzinger::implicit_btree_layout_policy<16>)

REGISTER_BATCH_BENCHMARKS(
  "Sorted", eytzinger::sorted_layout_policy, "String", std::string)
REGISTER_BATCH_BENCHMARKS(
  "Eytzinger", eytzinger::eytzinger_layout_policy<6>, "String", std::string)
REGISTER_BATCH_BENCHMARKS(
  "BTree", eytzinger::implicit_btree_layout_policy<16>, "String", std::string)

// --- Baseline (Single-at-a-time) Benchmark Function ---

template <typename LayoutPolicy, typename KeyT>
static void BM_Batch_Lookup_BASELINE(benchmark::State& state)
{
  const size_t n           = state.range(0);
  const size_t num_needles = 2048; // Same as batch tests

  auto                              keys = generate_random_data<KeyT>(n);
  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(keys.size());
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0);
  }

  using MapType = layout_map<KeyT, int, std::less<KeyT>, LayoutPolicy>;
  MapType map(pairs.begin(), pairs.end());

  auto needles = generate_random_data<KeyT>(num_needles);

  using NeedleIter = typename std::vector<KeyT>::const_iterator;
  using MapIter    = typename MapType::const_iterator;
  using ResultPair = std::pair<NeedleIter, MapIter>;

  std::vector<ResultPair> results;
  results.reserve(num_needles);

  for (auto _ : state) {
    results.clear();
    // The Baseline: Serial lookups in a loop
    for (auto it = needles.cbegin(); it != needles.cend(); ++it) {
      results.emplace_back(it, map.find(*it));
    }
    benchmark::DoNotOptimize(results.data());
  }

  state.SetItemsProcessed(state.iterations() * num_needles);
}

// --- Fixed Registration Macros ---

// Register the baseline for a specific Layout + Type
#define REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, KeyName, KeyType) \
  BENCHMARK_TEMPLATE(BM_Batch_Lookup_BASELINE, LayoutType, KeyType)            \
  BATCH_ARGS->Name(                                                            \
    LayoutName "/" KeyName "/SerialBaseline"); // <--- Added semicolon here

// Register baseline for all integral types
#define REGISTER_ALL_BASELINES(LayoutName, LayoutType)                         \
  REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, "int8", int8_t)         \
  REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, "uint8", uint8_t)       \
  REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, "int16", int16_t)       \
  REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, "uint16", uint16_t)     \
  REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, "uint32", uint32_t)     \
  REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, "int64", int64_t)       \
  REGISTER_BASELINE_BENCHMARKS(LayoutName, LayoutType, "uint64", uint64_t)

// --- Benchmarks Registration ---

// 1. Sorted Baseline
REGISTER_ALL_BASELINES("Sorted", eytzinger::sorted_layout_policy)

// 2. Eytzinger Baseline
REGISTER_ALL_BASELINES("Eytzinger", eytzinger::eytzinger_layout_policy<6>)

// 3. B-Tree Baseline
REGISTER_ALL_BASELINES("BTree", eytzinger::implicit_btree_layout_policy<16>)

// 4. String Baselines
REGISTER_BASELINE_BENCHMARKS(
  "Sorted", eytzinger::sorted_layout_policy, "String", std::string)
REGISTER_BASELINE_BENCHMARKS(
  "Eytzinger", eytzinger::eytzinger_layout_policy<6>, "String", std::string)
REGISTER_BASELINE_BENCHMARKS(
  "BTree", eytzinger::implicit_btree_layout_policy<16>, "String", std::string)

BENCHMARK_MAIN();
