#include <benchmark/benchmark.h>

#include <cmath>
#include <cstddef>
#include <deque>
#include <memory_resource>
#include <random>
#include <vector>

// Include your header here
#include <vault/segmented_vector/segmented_vector.hpp>

// -----------------------------------------------------------------------------
// Benchmarking Config
// -----------------------------------------------------------------------------
using ValueType =
    size_t; // Simple type to measure container overhead specifically

// We compare against std::deque (stable refs) and std::vector (baseline,
// unstable refs) We also include std::list just to show how slow non-contiguous
// memory is.

// -----------------------------------------------------------------------------
// 1. Pure Append Benchmark (Push Back)
// -----------------------------------------------------------------------------

template <typename Container> static void BM_PushBack(benchmark::State& state)
{
  for (auto _ : state) {
    Container c;
    // prevent optimization
    benchmark::DoNotOptimize(c);
    for (int i = 0; i < state.range(0); ++i) {
      c.push_back(i);
    }
    benchmark::ClobberMemory();
  }
  state.SetComplexityN(state.range(0));
}

// Register for segmented_vector
BENCHMARK_TEMPLATE(BM_PushBack, segmented_vector<ValueType>)
    ->RangeMultiplier(8)
    ->Range(8, 2 << 15)
    ->Complexity();

// Register for std::deque
BENCHMARK_TEMPLATE(BM_PushBack, std::deque<ValueType>)
    ->RangeMultiplier(8)
    ->Range(8, 2 << 15)
    ->Complexity();

// Register for std::vector
BENCHMARK_TEMPLATE(BM_PushBack, std::vector<ValueType>)
    ->RangeMultiplier(8)
    ->Range(8, 2 << 15)
    ->Complexity();

// -----------------------------------------------------------------------------
// 2. Pure Random Access Benchmark
// -----------------------------------------------------------------------------

template <typename Container>
static void BM_RandomAccess(benchmark::State& state)
{
  // Setup container
  size_t    N = state.range(0);
  Container c;
  for (size_t i = 0; i < N; ++i) {
    c.push_back(i);
  }

  // Generate random indices beforehand to strictly measure access time, not RNG
  // time
  std::vector<size_t>                   indices(10000); // 10k batch
  std::mt19937                          rng(12345);
  std::uniform_int_distribution<size_t> dist(0, N - 1);
  for (auto& idx : indices) {
    idx = dist(rng);
  }

  size_t batch_idx = 0;

  for (auto _ : state) {
    // Access 1000 random elements per iteration
    size_t sum = 0;
    for (int i = 0; i < 1000; ++i) {
      // Access
      sum += c[indices[batch_idx++ % indices.size()]];
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * 1000);
}

BENCHMARK_TEMPLATE(BM_RandomAccess, segmented_vector<ValueType>)
    ->Range(1 << 10, 1 << 20); // 1K to 1M elements

BENCHMARK_TEMPLATE(BM_RandomAccess, std::deque<ValueType>)
    ->Range(1 << 10, 1 << 20);

BENCHMARK_TEMPLATE(BM_RandomAccess, std::vector<ValueType>)
    ->Range(1 << 10, 1 << 20);

// -----------------------------------------------------------------------------
// 3. Read/Write Ratio (The "Tipping Point" Test)
// -----------------------------------------------------------------------------
// This benchmark simulates a "real world" loop where we occasionally append
// but mostly read, or vice versa.
//
// Arguments:
// Arg(0): Total size (fixed at 100,000 for this test)
// Arg(1): Read Percentage (0 to 100).
//         0 = All Appends. 100 = All Reads.
// -----------------------------------------------------------------------------

template <typename Container>
static void BM_MixedReadWrite(benchmark::State& state)
{
  size_t current_size    = 0;
  size_t max_size        = state.range(0); // e.g., 100,000
  long   read_percentage = state.range(1);

  // Pre-fill a bit so we aren't reading empty
  Container c;
  for (int i = 0; i < 1000; ++i) {
    c.push_back(i);
  }
  current_size = 1000;

  std::mt19937                        rng(12345);
  std::uniform_int_distribution<long> op_dist(0, 99);
  std::uniform_int_distribution<size_t>
      idx_dist; // distribution changes as size grows

  for (auto _ : state) {
    state.PauseTiming();
    // Re-generate distribution range based on current size
    // (Expensive, so we pause timing, though realistic workload might not)
    idx_dist = std::uniform_int_distribution<size_t>(0, current_size - 1);
    long op  = op_dist(rng);
    state.ResumeTiming();

    if (op < read_percentage) {
      // READ Operation
      size_t idx = idx_dist(rng);
      auto   val = c[idx];
      benchmark::DoNotOptimize(val);
    } else {
      // WRITE (Append) Operation
      c.push_back(static_cast<ValueType>(current_size));
      current_size++;
    }
  }
}

// Define the arguments: Size fixed at 100k. Vary Read %: 0, 10, 50, 90, 99,
// 100.
static void CustomArguments(benchmark::internal::Benchmark* b)
{
  // Check ratios at 100k elements
  int N = 100000;
  b->Args({N, 0});   // 100% Write
  b->Args({N, 20});  // 80% Write, 20% Read
  b->Args({N, 50});  // 50% Write, 50% Read
  b->Args({N, 80});  // 20% Write, 80% Read
  b->Args({N, 90});  // 10% Write, 90% Read
  b->Args({N, 95});  // 5% Write, 95% Read
  b->Args({N, 99});  // 1% Write, 99% Read
  b->Args({N, 100}); // 100% Read
}

BENCHMARK_TEMPLATE(BM_MixedReadWrite, segmented_vector<ValueType>)
    ->Apply(CustomArguments);
BENCHMARK_TEMPLATE(BM_MixedReadWrite, std::deque<ValueType>)
    ->Apply(CustomArguments);
BENCHMARK_TEMPLATE(BM_MixedReadWrite, std::vector<ValueType>)
    ->Apply(CustomArguments);

// Define a PMR-compatible alias for the segmented_vector
using PmrSegmentedVector =
    segmented_vector<size_t, std::pmr::polymorphic_allocator<size_t>>;

static void BM_PushBack_Monotonic(benchmark::State& state)
{
  size_t N = state.range(0);

  // Calculate a buffer size large enough to hold N elements + overhead (spine,
  // etc.) We multiply by 2 to be safe and ensure we never hit the upstream
  // allocator.
  size_t buffer_size = (N * sizeof(size_t)) * 2 + 4096;

  // Allocate the raw memory ONCE outside the timing loop.
  // This removes 'malloc' and 'mmap' costs entirely from the benchmark
  // measurements.
  std::vector<std::byte> buffer(buffer_size);

  for (auto _ : state) {
    // Construct a monotonic resource over the pre-allocated buffer.
    // This is extremely fast (just pointer initialization).
    std::pmr::monotonic_buffer_resource
        pool(buffer.data(), buffer.size(), std::pmr::null_memory_resource());

    // Create vector using the pool
    PmrSegmentedVector sv(&pool);

    // Perform the actual work
    for (int i = 0; i < N; ++i) {
      sv.push_back(i);
    }

    // Destructor of sv runs here, but no deallocation happens
    // because the pool owns the memory.
  }
  state.SetComplexityN(state.range(0));
}

// Register the benchmark with the same ranges as before
BENCHMARK(BM_PushBack_Monotonic)
    ->RangeMultiplier(8)
    ->Range(8, 2 << 15)
    ->Complexity();

// -----------------------------------------------------------------------------
// 4. Iteration Benchmark (Traverse All Elements)
// -----------------------------------------------------------------------------

template <typename Container> static void BM_Iteration(benchmark::State& state)
{
  size_t    N = state.range(0);
  Container c;
  // Fill container
  for (size_t i = 0; i < N; ++i) {
    c.push_back(static_cast<typename Container::value_type>(i));
  }

  for (auto _ : state) {
    // Use a volatile sink to ensure the loop isn't optimized away,
    // but keep the operation simple (addition) to expose iterator overhead.
    size_t sum = 0;
    for (const auto& val : c) {
      sum += val;
    }
    benchmark::DoNotOptimize(sum);
  }

  // Report items processed per second (throughput)
  state.SetItemsProcessed(state.iterations() * N);
}

BENCHMARK_TEMPLATE(BM_Iteration, segmented_vector<size_t>)
    ->Range(1 << 12, 1 << 20); // 4k to 1M elements

BENCHMARK_TEMPLATE(BM_Iteration, std::deque<size_t>)->Range(1 << 12, 1 << 20);

BENCHMARK_TEMPLATE(BM_Iteration, std::vector<size_t>)->Range(1 << 12, 1 << 20);

template <typename Container>
static void BM_ForEachSegment(benchmark::State& state)
{
  size_t    N = state.range(0);
  Container c;
  for (size_t i = 0; i < N; ++i) {
    c.push_back(i);
  }

  for (auto _ : state) {
    size_t sum = 0;
    // Lambda inlines perfectly, exposing the addition to the optimizer
    c.for_each_segment([&](size_t val) { sum += val; });
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * N);
}

BENCHMARK_TEMPLATE(BM_ForEachSegment, segmented_vector<size_t>)
    ->Range(1 << 12, 1 << 20);

BENCHMARK_MAIN();
