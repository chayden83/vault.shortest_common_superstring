#include <benchmark/benchmark.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <vector>

#include <vault/frozen_vector/frozen_vector.hpp>
#include <vault/frozen_vector/frozen_vector_builder.hpp>
#include <vault/frozen_vector/local_shared_storage_policy.hpp>
#include <vault/frozen_vector/shared_storage_policy.hpp>
#include <vault/frozen_vector/unique_storage_policy.hpp>

using namespace frozen;

// ============================================================================
// CONFIGURATION
// ============================================================================

// Define types for easier usage
using StdVec    = std::vector<int>;
using AtomicVec = frozen_vector_builder<int, shared_storage_policy<int>>;
using UniqueVec = frozen_vector_builder<int, unique_storage_policy<int>>;
using LocalVec  = frozen_vector_builder<int, local_shared_storage_policy<int>>;

// ============================================================================
// BENCHMARK: Copying (The Key Differentiator)
// ============================================================================

static void BM_StdVector_Copy(benchmark::State& state)
{
  size_t N = state.range(0);
  StdVec src(N);
  for (auto _ : state) {
    auto copy = src;
    benchmark::DoNotOptimize(copy.data());
  }
}

static void BM_AtomicShared_Copy(benchmark::State& state)
{
  size_t    N = state.range(0);
  AtomicVec builder(N);
  auto      src = std::move(builder).freeze();
  for (auto _ : state) {
    auto copy = src;
    benchmark::DoNotOptimize(copy.data());
  }
}

static void BM_LocalShared_Copy(benchmark::State& state)
{
  size_t   N = state.range(0);
  LocalVec builder(N);
  // Explicitly freeze to local const handle
  auto src = std::move(builder).freeze<local_shared_ptr<const int[]>>();
  for (auto _ : state) {
    auto copy = src;
    benchmark::DoNotOptimize(copy.data());
  }
}

// ============================================================================
// BENCHMARK: Construction (Allocation Overhead)
// ============================================================================

static void BM_StdVector_Construct(benchmark::State& state)
{
  size_t N = state.range(0);
  for (auto _ : state) {
    StdVec v(N);
    benchmark::DoNotOptimize(v.data());
  }
}

static void BM_AtomicShared_Construct(benchmark::State& state)
{
  size_t N = state.range(0);
  for (auto _ : state) {
    AtomicVec v(N);
    benchmark::DoNotOptimize(v.data());
  }
}

static void BM_Unique_Construct(benchmark::State& state)
{
  size_t N = state.range(0);
  for (auto _ : state) {
    UniqueVec v(N);
    benchmark::DoNotOptimize(v.data());
  }
}

static void BM_LocalShared_Construct(benchmark::State& state)
{
  size_t N = state.range(0);
  for (auto _ : state) {
    LocalVec v(N);
    benchmark::DoNotOptimize(v.data());
  }
}

// ============================================================================
// BENCHMARK: Interleaved Work (Latency Hiding)
// Purpose: Demonstrate how non-atomic ops allow ILP (Instruction Level
// Parallelism) to hide the cost of the copy, whereas atomics force
// serialization.
// ============================================================================

template <typename VectorType>
static void BM_InterleavedWork(benchmark::State& state)
{
  size_t     N = 1000; // Small vector to keep data in cache
  VectorType builder(N);
  auto       src = std::move(builder).freeze();

  // Volatile to prevent compiler from optimizing away the math
  volatile int input = 42;

  for (auto _ : state) {
    int a = input;
    int b = 1;

    // Block 1: Heavy Math (Independent of Vector)
    // Modern CPUs can execute 4+ of these per cycle if not stalled.
    a += b;
    b += a;
    a += b;
    b += a;
    a += b;
    b += a;
    a += b;
    b += a;

    // INTERFERENCE: Copy the vector
    // Atomic: Acts as a fence. Stops the math pipeline.
    // Local: CPU schedules this independently.
    auto copy = src;

    // Block 2: More Math
    a += b;
    b += a;
    a += b;
    b += a;
    a += b;
    b += a;
    a += b;
    b += a;

    // Prevent optimization of both the math and the copy
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(copy.data());
  }
}

// ============================================================================
// REGISTER BENCHMARKS
// ============================================================================

BENCHMARK(BM_StdVector_Copy)->Range(1024, 1024 * 1024);
BENCHMARK(BM_AtomicShared_Copy)->Range(1024, 1024 * 1024);
BENCHMARK(BM_LocalShared_Copy)->Range(1024, 1024 * 1024);

BENCHMARK(BM_StdVector_Construct)->Range(1024, 1024 * 1024);
BENCHMARK(BM_AtomicShared_Construct)->Range(1024, 1024 * 1024);
BENCHMARK(BM_Unique_Construct)->Range(1024, 1024 * 1024);
BENCHMARK(BM_LocalShared_Construct)->Range(1024, 1024 * 1024);

BENCHMARK_TEMPLATE(BM_InterleavedWork, AtomicVec);
BENCHMARK_TEMPLATE(BM_InterleavedWork, LocalVec);

BENCHMARK_MAIN();
