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
  std::vector<int> data(n);
  std::mt19937     rng(42);
  std::uniform_int_distribution<int>
      dist(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());

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
BENCHMARK_TEMPLATE(BM_Lookup_Random, sorted_map, int)       BENCH_ARGS_INT->Name("Int/Sorted/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration,     sorted_map, int)       BENCH_ARGS_INT->Name("Int/Sorted/Iterate");
BENCHMARK_TEMPLATE(BM_Construction,  sorted_map, int)       BENCH_ARGS_INT->Name("Int/Sorted/Construct");

// 2. Eytzinger Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, eytzinger_map, int)    BENCH_ARGS_INT->Name("Int/Eytzinger/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration,     eytzinger_map, int)    BENCH_ARGS_INT->Name("Int/Eytzinger/Iterate");
BENCHMARK_TEMPLATE(BM_Construction,  eytzinger_map, int)    BENCH_ARGS_INT->Name("Int/Eytzinger/Construct");

// 3. Implicit B-Tree Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, implicit_btree_map, int) BENCH_ARGS_INT->Name("Int/BTree/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration,     implicit_btree_map, int) BENCH_ARGS_INT->Name("Int/BTree/Iterate");
BENCHMARK_TEMPLATE(BM_Construction,  implicit_btree_map, int) BENCH_ARGS_INT->Name("Int/BTree/Construct");

// --- STRING BENCHMARKS ---

// 1. Sorted Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, sorted_map, std::string)       BENCH_ARGS_STR->Name("Str/Sorted/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration,     sorted_map, std::string)       BENCH_ARGS_STR->Name("Str/Sorted/Iterate");
BENCHMARK_TEMPLATE(BM_Construction,  sorted_map, std::string)       BENCH_ARGS_STR->Name("Str/Sorted/Construct");

// 2. Eytzinger Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, eytzinger_map, std::string)    BENCH_ARGS_STR->Name("Str/Eytzinger/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration,     eytzinger_map, std::string)    BENCH_ARGS_STR->Name("Str/Eytzinger/Iterate");
BENCHMARK_TEMPLATE(BM_Construction,  eytzinger_map, std::string)    BENCH_ARGS_STR->Name("Str/Eytzinger/Construct");

// 3. Implicit B-Tree Map
BENCHMARK_TEMPLATE(BM_Lookup_Random, btree_map, std::string)        BENCH_ARGS_STR->Name("Str/BTree/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration,     btree_map, std::string)        BENCH_ARGS_STR->Name("Str/BTree/Iterate");
BENCHMARK_TEMPLATE(BM_Construction,  btree_map, std::string)        BENCH_ARGS_STR->Name("Str/BTree/Construct");

// --- STRING VIEW ARENA BENCHMARKS (Optimized) ---

// 1. Sorted Map (View)
BENCHMARK_TEMPLATE(BM_Lookup_StringView_Arena, sorted_map)    BENCH_ARGS_STR->Name("View/Sorted/Lookup");

// 2. Eytzinger Map (View)
BENCHMARK_TEMPLATE(BM_Lookup_StringView_Arena, eytzinger_map) BENCH_ARGS_STR->Name("View/Eytzinger/Lookup");

// 3. Implicit B-Tree Map (View)
BENCHMARK_TEMPLATE(BM_Lookup_StringView_Arena, btree_map)     BENCH_ARGS_STR->Name("View/BTree/Lookup");

BENCHMARK_MAIN();
