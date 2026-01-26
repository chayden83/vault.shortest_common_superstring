#include <benchmark/benchmark.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

// Include your library headers
#include <vault/frozen_vector/frozen_vector.hpp>
#include <vault/frozen_vector/frozen_vector_builder.hpp>
#include <vault/frozen_vector/shared_storage_policy.hpp>

using namespace frozen;

// ============================================================================
// BENCHMARK 1: Linear Iteration (Read Performance)
// Purpose: Detect if shared_ptr dereferencing adds overhead vs std::vector
// ptrs.
// ============================================================================

static void BM_StdVector_Iterate(benchmark::State &state)
{
  // Setup (Outside timing loop)
  size_t N = state.range(0);
  std::vector<int> v(N);
  std::iota(v.begin(), v.end(), 0);

  for (auto _ : state) {
    long long sum = 0;
    for (auto x : v) {
      sum += x;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * N);
}

static void BM_FrozenVector_Iterate(benchmark::State &state)
{
  size_t N = state.range(0);
  frozen_vector_builder<int> builder(N);
  std::iota(builder.begin(), builder.end(), 0);
  auto v = std::move(builder).freeze();

  for (auto _ : state) {
    long long sum = 0;
    for (auto x : v) {
      sum += x;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * N);
}

// ============================================================================
// BENCHMARK 2: Random Access
// Purpose: Test operator[] overhead and cache locality.
// ============================================================================

static void BM_StdVector_RandomAccess(benchmark::State &state)
{
  size_t N = state.range(0);
  std::vector<int> v(N);
  std::iota(v.begin(), v.end(), 0);

  // Pre-calculate indices to avoid benchmarking the RNG
  std::vector<size_t> indices(1024);
  std::mt19937 gen(42);
  std::uniform_int_distribution<size_t> dist(0, N - 1);
  for (auto &i : indices)
    i = dist(gen);

  for (auto _ : state) {
    long long sum = 0;
    for (auto idx : indices) {
      sum += v[idx];
    }
    benchmark::DoNotOptimize(sum);
  }
}

static void BM_FrozenVector_RandomAccess(benchmark::State &state)
{
  size_t N = state.range(0);
  frozen_vector_builder<int> builder(N);
  std::iota(builder.begin(), builder.end(), 0);
  auto v = std::move(builder).freeze();

  std::vector<size_t> indices(1024);
  std::mt19937 gen(42);
  std::uniform_int_distribution<size_t> dist(0, N - 1);
  for (auto &i : indices)
    i = dist(gen);

  for (auto _ : state) {
    long long sum = 0;
    for (auto idx : indices) {
      sum += v[idx];
    }
    benchmark::DoNotOptimize(sum);
  }
}

// ============================================================================
// BENCHMARK 3: Copying (The Key Differentiator)
// Purpose: Compare Deep Copy (std::vector) vs Shallow Copy (frozen_vector).
// ============================================================================

static void BM_StdVector_Copy(benchmark::State &state)
{
  size_t N = state.range(0);
  std::vector<int> src(N);

  for (auto _ : state) {
    // Measures allocation + element-wise copy
    std::vector<int> copy = src;
    benchmark::DoNotOptimize(copy.data());
  }
}

static void BM_FrozenVector_Copy(benchmark::State &state)
{
  size_t N = state.range(0);
  frozen_vector_builder<int> builder(N);
  auto src = std::move(builder).freeze();

  for (auto _ : state) {
    // Measures atomic reference count increment
    auto copy = src;
    benchmark::DoNotOptimize(copy.data());
  }
}

// ============================================================================
// BENCHMARK 4: Construction (Allocation)
// Purpose: Compare allocate_shared_for_overwrite vs std::vector allocation.
// ============================================================================

static void BM_StdVector_Construct(benchmark::State &state)
{
  size_t N = state.range(0);
  for (auto _ : state) {
    // Note: std::vector<int>(N) performs zero-initialization!
    std::vector<int> v(N);
    benchmark::DoNotOptimize(v.data());
  }
}

static void BM_FrozenVectorBuilder_Construct(benchmark::State &state)
{
  size_t N = state.range(0);
  for (auto _ : state) {
    // Note: frozen_vector uses default initialization (no memset to 0)
    // This is strictly faster for POD types.
    frozen_vector_builder<int> v(N);
    benchmark::DoNotOptimize(v.data());
  }
}

// ============================================================================
// CONFIGURATION
// ============================================================================

// Register Benchmarks
// Ranges: 1KB, 256KB, 1MB elements to test L1/L2/RAM behaviors
BENCHMARK(BM_StdVector_Iterate)->Range(1024, 1024 * 1024);
BENCHMARK(BM_FrozenVector_Iterate)->Range(1024, 1024 * 1024);

BENCHMARK(BM_StdVector_RandomAccess)->Range(1024, 1024 * 1024);
BENCHMARK(BM_FrozenVector_RandomAccess)->Range(1024, 1024 * 1024);

BENCHMARK(BM_StdVector_Copy)->Range(1024, 1024 * 1024);
BENCHMARK(BM_FrozenVector_Copy)->Range(1024, 1024 * 1024);

BENCHMARK(BM_StdVector_Construct)->Range(1024, 1024 * 1024);
BENCHMARK(BM_FrozenVectorBuilder_Construct)->Range(1024, 1024 * 1024);

BENCHMARK_MAIN();
