#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <benchmark/benchmark.h>

#include <vault/algorithm/amac.hpp>
#include <vault/flat_map/aliases.hpp>
#include <vault/flat_map/eytzinger_layout_policy.hpp>
#include <vault/flat_map/implicit_btree_layout_policy.hpp>
#include <vault/flat_map/sorted_layout_policy.hpp>

using namespace eytzinger;

// ============================================================================
//  1. Data Generation & Configuration
// ============================================================================

// --- Constants ---
static constexpr size_t kNumNeedles     = 2048;
static constexpr size_t kAMACBufferSize = 16;

// --- Random Data Generators ---

template <typename T> struct DataGenerator {
  static std::vector<T> generate(size_t n, uint64_t seed = 42)
  {
    std::vector<T> data;
    data.reserve(n);
    std::mt19937_64 rng(seed);

    if constexpr (std::is_integral_v<T>) {
      using DistT = std::conditional_t<(sizeof(T) < 2), int16_t, T>;
      std::uniform_int_distribution<DistT> dist(
        std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
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
};

// --- Arena Helper for StringView Benchmarks ---
struct StringArena {
  std::vector<char>             buffer;
  std::vector<std::string_view> views;

  static StringArena generate_sorted(size_t n, size_t len = 32)
  {
    auto strings = DataGenerator<std::string>::generate(n);
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
};

// ============================================================================
//  2. Operation Strategies
// ============================================================================

// Strategy: Serial (Single-item) Find
struct OpSerialFind {
  static std::string name() { return "Serial/Find"; }

  template <typename Map, typename Needles, typename Results>
  static void run(const Map& map, const Needles& needles, Results& results)
  {
    for (auto it = needles.cbegin(); it != needles.cend(); ++it) {
      results.emplace_back(it, map.find(*it));
    }
  }
};

// Strategy: Batch Find
struct OpBatchFind {
  static std::string name() { return "Batch/Find"; }

  template <typename Map, typename Needles, typename Results>
  static void run(const Map& map, const Needles& needles, Results& results)
  {
    map.batch_find(vault::amac::coordinator<kAMACBufferSize>,
      needles,
      std::back_inserter(results));
  }
};

// Strategy: Batch Lower Bound
struct OpBatchLowerBound {
  static std::string name() { return "Batch/LowerBound"; }

  template <typename Map, typename Needles, typename Results>
  static void run(const Map& map, const Needles& needles, Results& results)
  {
    map.batch_lower_bound(vault::amac::coordinator<kAMACBufferSize>,
      needles,
      std::back_inserter(results));
  }
};

// Strategy: Batch Upper Bound
struct OpBatchUpperBound {
  static std::string name() { return "Batch/UpperBound"; }

  template <typename Map, typename Needles, typename Results>
  static void run(const Map& map, const Needles& needles, Results& results)
  {
    map.batch_upper_bound(vault::amac::coordinator<kAMACBufferSize>,
      needles,
      std::back_inserter(results));
  }
};

// ============================================================================
//  3. Core Benchmark Templates
// ============================================================================

/**
 * @brief Generic benchmark for Lookup operations (Serial or Batch).
 */
template <typename LayoutPolicy, typename KeyT, typename Operation>
static void BM_Lookup(benchmark::State& state)
{
  const size_t n = state.range(0);

  // 1. Prepare Haystack
  auto                              keys = DataGenerator<KeyT>::generate(n);
  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(keys.size());
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0); // Value doesn't matter for lookup perf
  }

  using MapType = layout_map<KeyT, int, std::less<KeyT>, LayoutPolicy>;
  MapType map(pairs.begin(), pairs.end());

  // 2. Prepare Needles
  auto needles = DataGenerator<KeyT>::generate(kNumNeedles, 123);

  // Result storage to prevent optimization
  using NeedleIter = typename std::vector<KeyT>::const_iterator;
  using MapIter    = typename MapType::const_iterator;
  std::vector<std::pair<NeedleIter, MapIter>> results;
  results.reserve(kNumNeedles);

  // 3. Run Benchmark
  for (auto _ : state) {
    results.clear();
    Operation::run(map, needles, results);
    benchmark::DoNotOptimize(results.data());
  }

  state.SetItemsProcessed(state.iterations() * kNumNeedles);
}

/**
 * @brief Benchmark for StringView Arena Lookups (Data Locality Test).
 */
template <template <typename...> class MapType>
static void BM_StringView_Arena(benchmark::State& state)
{
  const size_t n = state.range(0);

  // 1. Generate Arena
  StringArena arena = StringArena::generate_sorted(n);

  // 2. Create Map (Views point to contiguous arena memory)
  std::vector<std::pair<std::string_view, int>> pairs;
  pairs.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    pairs.emplace_back(arena.views[i], static_cast<int>(i));
  }
  MapType<std::string_view, int> map(pairs.begin(), pairs.end());

  // 3. Setup Random Lookups
  std::vector<std::string_view> lookups = arena.views;
  std::mt19937                  rng(123);
  std::shuffle(lookups.begin(), lookups.end(), rng);

  size_t idx = 0;
  for (auto _ : state) {
    const auto& key = lookups[idx];
    auto        it  = map.find(key);
    benchmark::DoNotOptimize(it);

    if (++idx >= n) {
      idx = 0;
    }
  }

  state.SetItemsProcessed(state.iterations());
}

/**
 * @brief Benchmark: Construction Cost
 */
template <typename LayoutPolicy, typename KeyT>
static void BM_Construction(benchmark::State& state)
{
  const size_t n    = state.range(0);
  auto         keys = DataGenerator<KeyT>::generate(n);

  std::vector<std::pair<KeyT, int>> pairs;
  pairs.reserve(n);
  for (const auto& k : keys) {
    pairs.emplace_back(k, 0);
  }

  using MapType = layout_map<KeyT, int, std::less<KeyT>, LayoutPolicy>;

  for (auto _ : state) {
    auto    local_pairs = pairs; // Copy to simulate fresh input
    MapType map(local_pairs.begin(), local_pairs.end());
    benchmark::DoNotOptimize(map);
  }
  state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================================
//  4. Registration Macros
// ============================================================================

// Standard arguments for lookup benchmarks
#define ARGS_LOOKUP                                                            \
  ->RangeMultiplier(4)->Range(256, 4 << 20)->Unit(benchmark::kNanosecond)

// Standard arguments for construction (can be slower, so smaller max range)
#define ARGS_CONSTRUCT                                                         \
  ->RangeMultiplier(4)->Range(256, 1 << 18)->Unit(benchmark::kNanosecond)

// Helper to register a specific combination
#define REGISTER_OP(LayoutName, LayoutType, KeyName, KeyType, OpStruct)        \
  BENCHMARK_TEMPLATE(BM_Lookup, LayoutType, KeyType, OpStruct)                 \
  ARGS_LOOKUP->Name(LayoutName "/" KeyName "/" + OpStruct::name());

#define REGISTER_CONSTRUCT(LayoutName, LayoutType, KeyName, KeyType)           \
  BENCHMARK_TEMPLATE(BM_Construction, LayoutType, KeyType)                     \
  ARGS_CONSTRUCT->Name(LayoutName "/" KeyName "/Construct");

// Register all operations for a specific Layout + Key combination
#define REGISTER_ALL_OPS(LayoutName, LayoutType, KeyName, KeyType)             \
  REGISTER_OP(LayoutName, LayoutType, KeyName, KeyType, OpSerialFind)          \
  REGISTER_OP(LayoutName, LayoutType, KeyName, KeyType, OpBatchFind)           \
  REGISTER_OP(LayoutName, LayoutType, KeyName, KeyType, OpBatchLowerBound)     \
  REGISTER_OP(LayoutName, LayoutType, KeyName, KeyType, OpBatchUpperBound)     \
  REGISTER_CONSTRUCT(LayoutName, LayoutType, KeyName, KeyType)

// Register for all standard integer types
#define REGISTER_INTEGRALS(LayoutName, LayoutType)                             \
  REGISTER_ALL_OPS(LayoutName, LayoutType, "int32", int32_t)                   \
  REGISTER_ALL_OPS(LayoutName, LayoutType, "int64", int64_t)

// ============================================================================
//  5. Benchmarks Registration
// ============================================================================

// --- A. Sorted Layouts (K-ary Variations) ---

// K=2 (Binary Search)
using SortedK2 = eytzinger::sorted_layout_policy<2>;
REGISTER_INTEGRALS("Sorted_K2", SortedK2)
REGISTER_ALL_OPS("Sorted_K2", SortedK2, "String", std::string)

// K=3 (Ternary Search)
using SortedK3 = eytzinger::sorted_layout_policy<3>;
REGISTER_INTEGRALS("Sorted_K3", SortedK3)

// K=4 (4-ary Search - Power of 2)
using SortedK4 = eytzinger::sorted_layout_policy<4>;
REGISTER_INTEGRALS("Sorted_K4", SortedK4)

// K=5 (5-ary Search - Prime, good for cache conflict avoidance)
using SortedK5 = eytzinger::sorted_layout_policy<5>;
REGISTER_INTEGRALS("Sorted_K5", SortedK5)

// K=8 (8-ary Search - Wide)
using SortedK8 = eytzinger::sorted_layout_policy<8>;
REGISTER_INTEGRALS("Sorted_K8", SortedK8)

// --- B. Eytzinger Layout ---

using EytzingerDefault = eytzinger::eytzinger_layout_policy<6>;
REGISTER_INTEGRALS("Eytzinger", EytzingerDefault)
REGISTER_ALL_OPS("Eytzinger", EytzingerDefault, "String", std::string)

// --- C. Implicit B-Tree Layout ---

using BTreeDefault = eytzinger::implicit_btree_layout_policy<16>;
REGISTER_INTEGRALS("BTree", BTreeDefault)
REGISTER_ALL_OPS("BTree", BTreeDefault, "String", std::string)

// --- D. StringView Arena Benchmarks ---

// Helper aliases for the Template template param required by
// BM_StringView_Arena
template <typename K, typename V>
using MapSortedK2 = layout_map<K, V, std::less<K>, SortedK2>;
template <typename K, typename V>
using MapEytzinger = layout_map<K, V, std::less<K>, EytzingerDefault>;
template <typename K, typename V>
using MapBTree = layout_map<K, V, std::less<K>, BTreeDefault>;

BENCHMARK_TEMPLATE(BM_StringView_Arena, MapSortedK2)
  ->RangeMultiplier(4)
  ->Range(256, 1 << 18)
  ->Unit(benchmark::kNanosecond)
  ->Name("ArenaView/Sorted_K2/SerialFind");

BENCHMARK_TEMPLATE(BM_StringView_Arena, MapEytzinger)
  ->RangeMultiplier(4)
  ->Range(256, 1 << 18)
  ->Unit(benchmark::kNanosecond)
  ->Name("ArenaView/Eytzinger/SerialFind");

BENCHMARK_TEMPLATE(BM_StringView_Arena, MapBTree)
  ->RangeMultiplier(4)
  ->Range(256, 1 << 18)
  ->Unit(benchmark::kNanosecond)
  ->Name("ArenaView/BTree/SerialFind");

BENCHMARK_MAIN();
