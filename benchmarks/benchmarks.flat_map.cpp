#include <limits>
#include <random>
#include <vector>
#include <algorithm>

#include <benchmark/benchmark.h>

#include <vault/flat_map/aliases.hpp>

// --- Configuration ---

// Define a B-Tree map alias locally for benchmarking
// B=16 implies 16 elements per block (approx 64 bytes for 4-byte ints)
template <typename K, typename V>
using implicit_btree_map = layout_map<K, V, std::less<K>, eytzinger::implicit_btree_layout_policy<16>>;

// Helper to generate random data
std::vector<int> generate_random_ints(size_t n) {
    std::vector<int> data(n);
    std::mt19937 rng(42); // Fixed seed for reproducibility
    // Use a wide range to minimize collisions but ensure coverage
    std::uniform_int_distribution<int> dist(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    
    for(size_t i=0; i<n; ++i) {
        data[i] = dist(rng);
    }
    return data;
}

// --- Benchmarks ---

/**
 * @brief Benchmark: Random Lookup (The core value proposition)
 * Compares the ability of the layout to hide memory latency during binary search.
 */
template <template<typename...> class MapType>
static void BM_Lookup_Random(benchmark::State& state) {
    const size_t n = state.range(0);
    
    // Setup Data
    auto keys = generate_random_ints(n);
    // Create map (construction time not measured here)
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(n);
    for(int k : keys) pairs.emplace_back(k, k);
    
    MapType<int, int> map(pairs.begin(), pairs.end());

    // Setup Search Keys (shuffle them so we access randomly)
    std::vector<int> lookups = keys;
    std::mt19937 rng(123);
    std::shuffle(lookups.begin(), lookups.end(), rng);

    // Run Benchmark
    size_t idx = 0;
    const size_t mask = n - 1; // Assuming n is power of 2 for fast wrapping, or just use %
    
    for (auto _ : state) {
        // Use a localized index to avoid expensive RNG inside the loop
        int key = lookups[idx];
        auto it = map.find(key);
        benchmark::DoNotOptimize(it);
        
        idx = (idx + 1);
        if (idx >= n) idx = 0;
    }
    
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("O(log N)");
}

/**
 * @brief Benchmark: Iteration (Full Scan)
 * Compares the overhead of 'smart' iterators vs raw pointer increments.
 * Expected: Sorted Map >> Eytzinger/BTree
 */
template <template<typename...> class MapType>
static void BM_Iteration(benchmark::State& state) {
    const size_t n = state.range(0);
    
    // Setup Data
    auto keys = generate_random_ints(n);
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(n);
    for(int k : keys) pairs.emplace_back(k, k);
    
    MapType<int, int> map(pairs.begin(), pairs.end());

    for (auto _ : state) {
        // Iterate entire container
        for (auto it = map.begin(); it != map.end(); ++it) {
            benchmark::DoNotOptimize(*it);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * n);
    state.SetLabel("O(N)");
}

/**
 * @brief Benchmark: Construction
 * Measures the cost of Sort + Permute.
 */
template <template<typename...> class MapType>
static void BM_Construction(benchmark::State& state) {
    const size_t n = state.range(0);
    auto keys = generate_random_ints(n);
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(n);
    for(int k : keys) pairs.emplace_back(k, k);

    for (auto _ : state) {
        // Destructor time is included, which is fair (memory cleanup)
        MapType<int, int> map(pairs.begin(), pairs.end());
        benchmark::DoNotOptimize(map);
    }
    
    state.SetItemsProcessed(state.iterations() * n);
    state.SetLabel("Sort + Permute");
}

// --- Register Benchmarks ---

// Range: 256 elements (L1) to 4 Million elements (RAM)
#define BENCH_ARGS ->RangeMultiplier(4)->Range(256, 32<<20)->Unit(benchmark::kNanosecond)

// 1. Sorted Map (Baseline)
BENCHMARK_TEMPLATE(BM_Lookup_Random, sorted_map) BENCH_ARGS->Name("SortedMap/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, sorted_map)     BENCH_ARGS->Name("SortedMap/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, sorted_map)  BENCH_ARGS->Name("SortedMap/Construct");

// 2. Eytzinger Map (Prefetching)
BENCHMARK_TEMPLATE(BM_Lookup_Random, eytzinger_map) BENCH_ARGS->Name("EytzingerMap/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, eytzinger_map)     BENCH_ARGS->Name("EytzingerMap/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, eytzinger_map)  BENCH_ARGS->Name("EytzingerMap/Construct");

// 3. Implicit B-Tree Map (Block Scan)
BENCHMARK_TEMPLATE(BM_Lookup_Random, implicit_btree_map) BENCH_ARGS->Name("BTreeMap/Lookup");
BENCHMARK_TEMPLATE(BM_Iteration, implicit_btree_map)     BENCH_ARGS->Name("BTreeMap/Iterate");
BENCHMARK_TEMPLATE(BM_Construction, implicit_btree_map)  BENCH_ARGS->Name("BTreeMap/Construct");

BENCHMARK_MAIN();
