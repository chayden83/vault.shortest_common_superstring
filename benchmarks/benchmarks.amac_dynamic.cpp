// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_DYNAMIC_BENCHMARKS_CPP
#define VAULT_AMAC_DYNAMIC_BENCHMARKS_CPP

#include <benchmark/benchmark.h>

#include <cmath>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <vector>

#include <vault/algorithm/amac_dynamic.hpp>

namespace vault::amac::benchmarks {

  // --------------------------------------------------------------------------
  // Global Fixture: Allocates and shuffles the 400MB memory pool exactly once.
  // --------------------------------------------------------------------------
  class PointerChaseFixture : public benchmark::Fixture {
  public:
    static std::vector<uint32_t> memory;
    static std::vector<uint32_t> initial_indices;
    static bool                  initialized;

    void SetUp(const benchmark::State&) override {
      if (!initialized) {
        // 100 million items * 4 bytes = ~400 MB (Blows out the L3 cache)
        memory.resize(100'000'000);
        std::iota(memory.begin(), memory.end(), 0);

        std::mt19937 gen(42);
        std::shuffle(memory.begin(), memory.end(), gen);

        // Pre-roll a massive batch of random starting indices
        initial_indices.resize(500'000);
        for (int i = 0; i < 500'000; ++i) {
          initial_indices[i] = gen() % memory.size();
        }
        initialized = true;
      }
    }
  };

  std::vector<uint32_t> PointerChaseFixture::memory;
  std::vector<uint32_t> PointerChaseFixture::initial_indices;
  bool                  PointerChaseFixture::initialized = false;

  // --------------------------------------------------------------------------
  // The AMAC Context & Job definitions
  // --------------------------------------------------------------------------
  struct pointer_chase_job {
    uint32_t index;
    uint32_t depth;
  };

  template <uint32_t M, uint32_t MaxDepth>
  struct dynamic_chase_context {
    std::span<uint32_t const> memory;

    [[nodiscard]] static constexpr std::size_t fanout() noexcept {
      return 1;
    }

    template <typename Sink>
    auto init(pointer_chase_job& job, Sink&) noexcept -> std::expected<vault::amac::step_result<1>, int> {
      // Issue the prefetch hint for the upcoming array read
      return vault::amac::step_result<1>{&memory[job.index]};
    }

    template <typename Sink>
    auto step(pointer_chase_job&, Sink&) noexcept -> std::expected<vault::amac::step_result<1>, int> {
      // The prefetch has had time to resolve; transition to finalize
      return vault::amac::step_result<1>{nullptr};
    }

    template <typename Sink>
    auto finalize(pointer_chase_job& job, Sink& sink) noexcept -> std::expected<std::optional<int>, int> {
      if (job.depth < MaxDepth) {
        // Read M contiguous pointers from the current node.
        // Because the memory array is shuffled, these values are random jumps.
        for (uint32_t i = 0; i < M; ++i) {
          uint32_t next_index = memory[(job.index + i) % memory.size()];
          sink(pointer_chase_job{next_index, job.depth + 1});
        }
      }
      return 1;
    }
  };

  // A lightweight reporter that just silently consumes completions
  struct dummy_reporter {
    void operator()(vault::amac::completed_tag, auto&&, auto&&) const noexcept {}

    void operator()(vault::amac::terminated_tag, auto&&) const noexcept {}

    void operator()(vault::amac::failed_tag, auto&&, auto&&) const noexcept {}
  };

  // Helper to calculate the total nodes visited in a uniform tree
  constexpr size_t calculate_total_nodes(uint32_t N, uint32_t M, uint32_t MaxDepth) {
    if (M == 1) {
      return N * (MaxDepth + 1);
    }
    size_t nodes_per_root = static_cast<size_t>((std::pow(M, MaxDepth + 1) - 1) / (M - 1));
    return N * nodes_per_root;
  }

  // --------------------------------------------------------------------------
  // The Benchmarks
  // --------------------------------------------------------------------------

  // AMAC Dynamic Executor Benchmark
  template <uint32_t B, uint32_t M, uint32_t MaxDepth, uint32_t N>
  void BM_DynamicAmac(benchmark::State& state) {
    dynamic_chase_context<M, MaxDepth> ctx{PointerChaseFixture::memory};
    dummy_reporter                     reporter{};

    size_t const items_processed = calculate_total_nodes(N, M, MaxDepth);

    for (auto _ : state) {
      std::vector<pointer_chase_job> queue;
      // Max queue size is initial roots + (max depth * fanout * concurrent AMAC slots)
      queue.reserve(N + (MaxDepth * M * B));

      for (uint32_t i = 0; i < N; ++i) {
        queue.push_back({PointerChaseFixture::initial_indices[i], 0});
      }

      auto source = [&]() -> std::optional<pointer_chase_job> {
        if (queue.empty()) {
          return std::nullopt;
        }
        auto j = queue.back();
        queue.pop_back();
        return j;
      };

      auto sink = [&](pointer_chase_job&& j) { queue.push_back(j); };

      vault::amac::dynamic_executor<B>(ctx, reporter, source, sink);
    }
    state.SetItemsProcessed(state.iterations() * items_processed);
  }

  // Serial Baseline Benchmark
  template <uint32_t B, uint32_t M, uint32_t MaxDepth, uint32_t N>
  void BM_SerialBaseline(benchmark::State& state) {
    std::span<uint32_t const> mem             = PointerChaseFixture::memory;
    size_t const              items_processed = calculate_total_nodes(N, M, MaxDepth);

    for (auto _ : state) {
      std::vector<pointer_chase_job> queue;
      queue.reserve(N + (MaxDepth * M * B));

      for (uint32_t i = 0; i < N; ++i) {
        queue.push_back({PointerChaseFixture::initial_indices[i], 0});
      }

      while (!queue.empty()) {
        auto job = queue.back();
        queue.pop_back();

        if (job.depth < MaxDepth) {
          for (uint32_t i = 0; i < M; ++i) {
            // Read the random pointer from memory into a local, mutable variable
            uint32_t next_index = mem[(job.index + i) % mem.size()];

            // Pass the mutable variable to DoNotOptimize to prevent the compiler
            // from optimizing away the read or the queue push
            benchmark::DoNotOptimize(next_index);

            queue.push_back({next_index, job.depth + 1});
          }
        }
      }
    }
    state.SetItemsProcessed(state.iterations() * items_processed);
  }

  // We use N = 50,000 roots.
  // With Fanout 4 and Depth 4 (341 nodes per root), this processes 17,050,000 nodes.
  // 17M nodes * 4 bytes = 68 MB per iteration, completely wiping the L1/L2/L3 caches.

  BENCHMARK_F(PointerChaseFixture, Serial_B16_M4)(benchmark::State& state) {
    BM_SerialBaseline<16, 4, 4, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Amac_B16_M4)(benchmark::State& state) {
    BM_DynamicAmac<16, 4, 4, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Serial_B32_M4)(benchmark::State& state) {
    BM_SerialBaseline<32, 4, 4, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Amac_B32_M4)(benchmark::State& state) {
    BM_DynamicAmac<32, 4, 4, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Serial_B64_M4)(benchmark::State& state) {
    BM_SerialBaseline<64, 4, 4, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Amac_B64_M4)(benchmark::State& state) {
    BM_DynamicAmac<64, 4, 4, 50000>(state);
  }

  // --------------------------------------------------------------------------
  // The Ultimate Test: Pure Linked-List Chase (Fanout 1)
  // No siblings to speculatively execute. MaxDepth 64. Roots 50,000.
  // --------------------------------------------------------------------------
  BENCHMARK_F(PointerChaseFixture, Serial_B16_M1)(benchmark::State& state) {
    BM_SerialBaseline<16, 1, 64, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Amac_B16_M1)(benchmark::State& state) {
    BM_DynamicAmac<16, 1, 64, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Serial_B32_M1)(benchmark::State& state) {
    BM_SerialBaseline<32, 1, 64, 50000>(state);
  }

  BENCHMARK_F(PointerChaseFixture, Amac_B32_M1)(benchmark::State& state) {
    BM_DynamicAmac<32, 1, 64, 50000>(state);
  }

} // namespace vault::amac::benchmarks

#endif // VAULT_AMAC_DYNAMIC_BENCHMARKS_CPP
