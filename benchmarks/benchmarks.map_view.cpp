#include "benchmarks.map_view.hpp"
#include <benchmark/benchmark.h>
#include <boost/unordered/unordered_flat_map.hpp>
#include <memory>

/**
 * @brief Baseline: Direct access to the Boost flat map.
 * The compiler can fully inline the hash and search logic here.
 */
static void BM_DirectAccess_Inlined(benchmark::State& state)
{
  const auto count = static_cast<std::size_t>(state.range(0));

  // We recreate a local container to allow the compiler to see the type
  boost::unordered_flat_map<std::string, int> container;
  std::vector<std::string>                    keys;

  for (std::size_t i = 0; i < count; ++i) {
    auto key       = "key_" + std::to_string(i);
    container[key] = static_cast<int>(i);
    keys.push_back(key);
  }

  auto       i    = 0u;
  const auto size = keys.size();

  for (auto _ : state) {
    const auto& key = keys[i++ % size];

    // Manual pointer extraction to match map_view's internal logic
    auto  it  = container.find(key);
    auto* ptr = (it != container.end()) ? std::addressof(it->second) : nullptr;

    benchmark::DoNotOptimize(ptr);
    benchmark::ClobberMemory();
  }
}

/**
 * @brief Overhead: Access through the type-erased map_view.
 * The view is created in a separate TU, forcing an indirect call.
 */
static void BM_ViewAccess_Indirect(benchmark::State& state)
{
  const auto  count = static_cast<std::size_t>(state.range(0));
  auto        view  = bench::get_opaque_view(count);
  const auto& keys  = bench::get_keys();

  auto       i    = 0u;
  const auto size = keys.size();

  for (auto _ : state) {
    const auto& key = keys[i++ % size];

    // This triggers the indirect VTable jump
    auto* ptr = view.find(key);

    benchmark::DoNotOptimize(ptr);
    benchmark::ClobberMemory();
  }
}

// Benchmarking with a typical cache-straining size
BENCHMARK(BM_DirectAccess_Inlined)->Arg(1000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_ViewAccess_Indirect)->Arg(1000)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
