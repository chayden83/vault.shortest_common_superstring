#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

#include <vault/unroll/unroll.hpp>

namespace {

  constexpr auto unroll_factor = std::size_t{8};

  // Helper to generate a realistic JSON-like payload with escaped characters
  auto generate_json_payload(std::size_t size) -> std::vector<char> {
    auto data = std::vector<char>(size, 'a');
    auto seed = std::uint64_t{12345};

    for (auto i = std::size_t{0}; i < size; ++i) {
      seed          = seed * 1103515245 + 12345;
      auto rand_val = seed % 100;

      if (rand_val < 5) {
        data[i] = '"'; // 5% chance of a quote
      } else if (rand_val < 8) {
        data[i] = '\\'; // 3% chance of a backslash
      }
    }
    return data;
  }

  // ============================================================================
  // Benchmark 1: Standard Loop
  // ============================================================================
  static void bm_standard_loop(benchmark::State& state) {
    auto size = static_cast<std::size_t>(state.range(0));
    auto data = generate_json_payload(size);

    for (auto _ : state) {
      auto quote_count = std::size_t{0};
      auto is_escaped  = false;

      for (auto i = std::size_t{0}; i < size; ++i) {
        auto c = data[i];
        if (c == '\\') {
          is_escaped = !is_escaped;
        } else {
          if (c == '"' && !is_escaped) {
            quote_count++;
          }
          is_escaped = false;
        }
      }

      benchmark::DoNotOptimize(quote_count);
    }
  }

  BENCHMARK(bm_standard_loop)->RangeMultiplier(8)->Range(1024, 8388608);

  // ============================================================================
  // Benchmark 2: Vault Explicit Unrolling
  // ============================================================================
  static void bm_vault_unroll(benchmark::State& state) {
    auto size = static_cast<std::size_t>(state.range(0));
    auto data = generate_json_payload(size);

    for (auto _ : state) {
      auto quote_count = std::size_t{0};
      auto is_escaped  = false;

      vault::unroll_loop<unroll_factor>(std::size_t{0}, size, [&](auto i) {
        auto c = data[i];
        if (c == '\\') {
          is_escaped = !is_escaped;
        } else {
          if (c == '"' && !is_escaped) {
            quote_count++;
          }
          is_escaped = false;
        }
      });

      benchmark::DoNotOptimize(quote_count);
    }
  }

  BENCHMARK(bm_vault_unroll)->RangeMultiplier(8)->Range(1024, 8388608);

  // ============================================================================
  // Benchmark 3: Pragma Unrolling
  // ============================================================================
  static void bm_pragma_unroll(benchmark::State& state) {
    auto size = static_cast<std::size_t>(state.range(0));
    auto data = generate_json_payload(size);

    for (auto _ : state) {
      auto quote_count = std::size_t{0};
      auto is_escaped  = false;

#if defined(__clang__)
#pragma clang loop unroll_count(8)
#elif defined(__GNUC__)
#pragma GCC unroll 8
#endif
      for (auto i = std::size_t{0}; i < size; ++i) {
        auto c = data[i];
        if (c == '\\') {
          is_escaped = !is_escaped;
        } else {
          if (c == '"' && !is_escaped) {
            quote_count++;
          }
          is_escaped = false;
        }
      }

      benchmark::DoNotOptimize(quote_count);
    }
  }

  BENCHMARK(bm_pragma_unroll)->RangeMultiplier(8)->Range(1024, 8388608);

} // namespace

BENCHMARK_MAIN();
