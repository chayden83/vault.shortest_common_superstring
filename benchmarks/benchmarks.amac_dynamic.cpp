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
        uint32_t base_val = memory[job.index];

        // The Unpredictable Branch
        if (base_val % 2 == 0) {
          // Even path: chase the immediate next indices
          for (uint32_t i = 0; i < M; ++i) {
            uint32_t next_index = memory[(job.index + 1 + i) % memory.size()];
            sink(pointer_chase_job{next_index, job.depth + 1});
          }
        } else {
          // Odd path: chase a completely different set of indices
          for (uint32_t i = 0; i < M; ++i) {
            uint32_t next_index = memory[(job.index + M + 1 + i) % memory.size()];
            sink(pointer_chase_job{next_index, job.depth + 1});
          }
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
          uint32_t base_val = mem[job.index];
          benchmark::DoNotOptimize(base_val); // Force the initial cache-miss load

          if (base_val % 2 == 0) {
            for (uint32_t i = 0; i < M; ++i) {
              uint32_t next_index = mem[(job.index + 1 + i) % mem.size()];
              benchmark::DoNotOptimize(next_index);
              queue.push_back({next_index, job.depth + 1});
            }
          } else {
            for (uint32_t i = 0; i < M; ++i) {
              uint32_t next_index = mem[(job.index + M + 1 + i) % mem.size()];
              benchmark::DoNotOptimize(next_index);
              queue.push_back({next_index, job.depth + 1});
            }
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

  // --------------------------------------------------------------------------
  // Real-World Scenario: Leveled DAG with Raw Pointer Traversal
  // --------------------------------------------------------------------------

  class LeveledDagFixture : public benchmark::Fixture {
  public:
    struct DagNode {
      uint64_t                              payload;
      std::vector<std::shared_ptr<DagNode>> children;
    };

    static std::vector<std::shared_ptr<DagNode>> all_nodes;
    static std::vector<DagNode*>                 initial_roots;
    static size_t                                total_traversal_nodes;
    static bool                                  initialized;

    void SetUp(const benchmark::State&) override {
      if (!initialized) {
        constexpr size_t NUM_NODES       = 2'000'000;
        constexpr size_t NUM_LEVELS      = 20;
        constexpr size_t NODES_PER_LEVEL = NUM_NODES / NUM_LEVELS; // 100,000 nodes per level

        all_nodes.reserve(NUM_NODES);
        for (size_t i = 0; i < NUM_NODES; ++i) {
          all_nodes.push_back(std::make_shared<DagNode>(DagNode{i, {}}));
        }

        std::mt19937 gen(42);

        // Shuffle the nodes so sequential array indices are geographically scattered
        // across the heap, guaranteeing cache misses when traversing levels.
        std::shuffle(all_nodes.begin(), all_nodes.end(), gen);

        // Generate Zipfian weights for degrees 0 through 8
        std::vector<double> weights;
        for (int k = 1; k <= 9; ++k) {
          weights.push_back(1.0 / std::pow(k, 1.5));
        }
        std::discrete_distribution<size_t> zipf_dist(weights.begin(), weights.end());

        // Construct the Leveled DAG
        for (size_t level = 0; level < NUM_LEVELS - 1; ++level) {
          size_t level_start      = level * NODES_PER_LEVEL;
          size_t next_level_start = (level + 1) * NODES_PER_LEVEL;

          std::uniform_int_distribution<size_t> child_dist(next_level_start, next_level_start + NODES_PER_LEVEL - 1);

          for (size_t i = 0; i < NODES_PER_LEVEL; ++i) {
            size_t degree = zipf_dist(gen); // Result is [0, 8]
            for (size_t d = 0; d < degree; ++d) {
              all_nodes[level_start + i]->children.push_back(all_nodes[child_dist(gen)]);
            }
          }
        }

        // Select the first 5,000 nodes of Level 0 as roots
        for (size_t i = 0; i < 5000; ++i) {
          initial_roots.push_back(all_nodes[i].get());
        }

        // Dry-run the traversal to count the exact number of visits for normalization
        total_traversal_nodes   = 0;
        std::vector<DagNode*> q = initial_roots;
        while (!q.empty()) {
          DagNode* curr = q.back();
          q.pop_back();
          ++total_traversal_nodes;
          for (auto const& c : curr->children) {
            q.push_back(c.get());
          }
        }

        initialized = true;
      }
    }
  };

  std::vector<std::shared_ptr<LeveledDagFixture::DagNode>> LeveledDagFixture::all_nodes;
  std::vector<LeveledDagFixture::DagNode*>                 LeveledDagFixture::initial_roots;
  size_t                                                   LeveledDagFixture::total_traversal_nodes = 0;
  bool                                                     LeveledDagFixture::initialized           = false;

  struct raw_ptr_dag_context {
    [[nodiscard]] static constexpr std::size_t fanout() noexcept {
      return 1;
    }

    template <typename Sink>
    auto init(LeveledDagFixture::DagNode* job, Sink&) noexcept -> std::expected<vault::amac::step_result<1>, int> {
      return vault::amac::step_result<1>{job};
    }

    template <typename Sink>
    auto step(LeveledDagFixture::DagNode*, Sink&) noexcept -> std::expected<vault::amac::step_result<1>, int> {
      return vault::amac::step_result<1>{nullptr};
    }

    template <typename Sink>
    auto finalize(LeveledDagFixture::DagNode* job, Sink& sink) noexcept -> std::expected<std::optional<int>, int> {
      benchmark::DoNotOptimize(job->payload);

      for (auto const& child : job->children) {
        sink(child.get()); // Pass the raw pointer to the LIFO queue
      }
      return 1;
    }
  };

  template <uint32_t B>
  void BM_LeveledDag_Amac(benchmark::State& state) {
    raw_ptr_dag_context ctx{};
    dummy_reporter      reporter{};

    for (auto _ : state) {
      std::vector<LeveledDagFixture::DagNode*> queue;
      queue.reserve(500'000);

      for (auto* root : LeveledDagFixture::initial_roots) {
        queue.push_back(root);
      }

      auto source = [&]() -> std::optional<LeveledDagFixture::DagNode*> {
        if (queue.empty()) {
          return std::nullopt;
        }
        auto* j = queue.back();
        queue.pop_back();
        return j;
      };

      auto sink = [&](LeveledDagFixture::DagNode* j) { queue.push_back(j); };

      vault::amac::dynamic_executor<B>(ctx, reporter, source, sink);
    }
    state.SetItemsProcessed(state.iterations() * LeveledDagFixture::total_traversal_nodes);
  }

  void BM_LeveledDag_Serial(benchmark::State& state) {
    for (auto _ : state) {
      std::vector<LeveledDagFixture::DagNode*> queue;
      queue.reserve(500'000);

      for (auto* root : LeveledDagFixture::initial_roots) {
        queue.push_back(root);
      }

      while (!queue.empty()) {
        auto* job = queue.back();
        queue.pop_back();

        benchmark::DoNotOptimize(job->payload);

        for (auto const& child : job->children) {
          queue.push_back(child.get());
        }
      }
    }
    state.SetItemsProcessed(state.iterations() * LeveledDagFixture::total_traversal_nodes);
  }

  BENCHMARK_F(LeveledDagFixture, Serial)(benchmark::State& state) {
    BM_LeveledDag_Serial(state);
  }

  BENCHMARK_F(LeveledDagFixture, Amac_B16)(benchmark::State& state) {
    BM_LeveledDag_Amac<16>(state);
  }

  BENCHMARK_F(LeveledDagFixture, Amac_B32)(benchmark::State& state) {
    BM_LeveledDag_Amac<32>(state);
  }

  BENCHMARK_F(LeveledDagFixture, Amac_B64)(benchmark::State& state) {
    BM_LeveledDag_Amac<64>(state);
  }

  
  
} // namespace vault::amac::benchmarks

#endif // VAULT_AMAC_DYNAMIC_BENCHMARKS_CPP
