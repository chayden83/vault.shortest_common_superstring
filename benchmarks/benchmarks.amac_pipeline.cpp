#include <benchmark/benchmark.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <ratio>
#include <string>
#include <vector>

#include <vault/algorithm/amac_chunked.hpp>
#include <vault/algorithm/amac_pipeline.hpp>

// ----------------------------------------------------------------------------
// 1. Contexts & Jobs
// ----------------------------------------------------------------------------
struct lower_bound_job {
  int         target_key;
  std::size_t low;
  std::size_t high;
  std::size_t mid;
};

struct lower_bound_context {
  std::vector<std::pair<int, int>> const* data;

  [[nodiscard]] static constexpr std::size_t fanout() noexcept {
    return 1;
  }

  [[nodiscard]] auto init(lower_bound_job& job) const -> vault::amac::step_result<1> {
    job.low  = 0;
    job.high = data->size();
    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return {&(*data)[job.mid]};
    }
    return {nullptr};
  }

  [[nodiscard]] auto step(lower_bound_job& job) const -> vault::amac::step_result<1> {
    if (job.low >= job.high) {
      return {nullptr};
    }

    if ((*data)[job.mid].first < job.target_key) {
      job.low = job.mid + 1;
    } else {
      job.high = job.mid;
    }

    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return {&(*data)[job.mid]};
    }
    return {nullptr};
  }
};

struct upper_bound_job {
  int         original_key;
  int         target_key;
  std::size_t low;
  std::size_t high;
  std::size_t mid;
};

struct upper_bound_context {
  std::vector<std::pair<int, int>> const* data;

  [[nodiscard]] static constexpr std::size_t fanout() noexcept {
    return 1;
  }

  [[nodiscard]] auto init(upper_bound_job& job) const -> vault::amac::step_result<1> {
    job.low  = 0;
    job.high = data->size();
    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return {&(*data)[job.mid]};
    }
    return {nullptr};
  }

  [[nodiscard]] auto step(upper_bound_job& job) const -> vault::amac::step_result<1> {
    if (job.low >= job.high) {
      return {nullptr};
    }

    if ((*data)[job.mid].first <= job.target_key) {
      job.low = job.mid + 1;
    } else {
      job.high = job.mid;
    }

    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return {&(*data)[job.mid]};
    }
    return {nullptr};
  }
};

struct point_lookup_job {
  int         original_key;
  int         target_key;
  std::size_t low;
  std::size_t high;
  std::size_t mid;
};

struct point_lookup_context {
  std::vector<std::pair<int, int>> const* data;

  [[nodiscard]] static constexpr std::size_t fanout() noexcept {
    return 1;
  }

  [[nodiscard]] auto init(point_lookup_job& job) const -> vault::amac::step_result<1> {
    job.low  = 0;
    job.high = data->size();
    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return {&(*data)[job.mid]};
    }
    return {nullptr};
  }

  [[nodiscard]] auto step(point_lookup_job& job) const -> vault::amac::step_result<1> {
    if (job.low >= job.high) {
      return {nullptr};
    }

    if ((*data)[job.mid].first < job.target_key) {
      job.low = job.mid + 1;
    } else if ((*data)[job.mid].first > job.target_key) {
      job.high = job.mid;
    } else {
      job.low  = job.mid;
      job.high = job.mid;
      return {nullptr};
    }

    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return {&(*data)[job.mid]};
    }
    return {nullptr};
  }
};

struct lower_to_upper_transition {
  template <typename CtxB>
  auto operator()(lower_bound_context const& ctx_a, CtxB const& /* ctx_b */, lower_bound_job const& job_a) const
    -> std::optional<upper_bound_job> {
    if (job_a.low < ctx_a.data->size() && (*ctx_a.data)[job_a.low].first == job_a.target_key) {
      auto mapped_value = (*ctx_a.data)[job_a.low].second;
      return upper_bound_job{
        .original_key = job_a.target_key, .target_key = mapped_value, .low = 0, .high = 0, .mid = 0
      };
    }
    return std::nullopt;
  }
};

struct upper_to_point_transition {
  template <typename CtxB>
  auto operator()(upper_bound_context const& ctx_a, CtxB const& /* ctx_b */, upper_bound_job const& job_a) const
    -> std::optional<point_lookup_job> {
    if (job_a.low > 0 && job_a.low <= ctx_a.data->size()) {
      auto mapped_value = (*ctx_a.data)[job_a.low - 1].second;
      return point_lookup_job{
        .original_key = job_a.original_key, .target_key = mapped_value, .low = 0, .high = 0, .mid = 0
      };
    }
    return std::nullopt;
  }
};

// ----------------------------------------------------------------------------
// 2. Data Generation Helper
// ----------------------------------------------------------------------------
struct benchmark_data {
  std::vector<std::pair<int, int>> stage1_data;
  std::vector<std::pair<int, int>> stage2_data;
  std::vector<std::pair<int, int>> stage3_data;
  std::vector<lower_bound_job>     queries;
};

[[nodiscard]] auto generate_data(
  std::size_t target_queries,
  double      transition_probability,
  std::size_t size_a,
  std::size_t size_b,
  std::size_t size_c = 65536
) -> benchmark_data {

  auto rng  = std::mt19937{42};
  auto data = benchmark_data{};

  data.stage1_data.reserve(size_a);
  for (std::size_t i = 0; i < size_a; ++i) {
    data.stage1_data.emplace_back(static_cast<int>(i * 2), static_cast<int>(i * 20));
  }

  data.stage2_data.reserve(size_b);
  for (std::size_t i = 0; i < size_b; ++i) {
    data.stage2_data.emplace_back(static_cast<int>(i * 20), static_cast<int>(i * 200));
  }

  data.stage3_data.reserve(size_c);
  for (std::size_t i = 0; i < size_c; ++i) {
    data.stage3_data.emplace_back(static_cast<int>(i * 200), static_cast<int>(i * 2000));
  }

  data.queries.reserve(target_queries);
  auto dist = std::uniform_real_distribution<double>{0.0, 1.0};

  for (std::size_t i = 0; i < target_queries; ++i) {
    if (dist(rng) <= transition_probability) {
      auto const valid_key = data.stage1_data[i % size_a].first;
      data.queries.push_back(lower_bound_job{valid_key, 0, 0, 0});
    } else {
      auto const invalid_key = data.stage1_data[i % size_a].first + 1;
      data.queries.push_back(lower_bound_job{invalid_key, 0, 0, 0});
    }
  }

  return data;
}

// ----------------------------------------------------------------------------
// 3. The 2-Stage Benchmarks
// ----------------------------------------------------------------------------
template <std::size_t Fanout>
void bm_sequential_amac(benchmark::State& state, double trans_prob, std::size_t size_a, std::size_t size_b) {
  auto const num_queries = static_cast<std::size_t>(state.range(0));
  auto       data        = generate_data(num_queries, trans_prob, size_a, size_b);

  auto ctx_a      = lower_bound_context{&data.stage1_data};
  auto ctx_b      = upper_bound_context{&data.stage2_data};
  auto transition = lower_to_upper_transition{};

  for (auto _ : state) {
    auto intermediate_buffer = std::vector<upper_bound_job>{};
    intermediate_buffer.reserve(num_queries);

    auto reporter_a = [&]<typename J>(J&& job) {
      if (auto opt_b = transition(ctx_a, ctx_b, job)) {
        intermediate_buffer.push_back(std::move(*opt_b));
      }
    };

    vault::amac::executor<Fanout>(data.queries, ctx_a, reporter_a);
    benchmark::DoNotOptimize(intermediate_buffer.data());

    auto total_found = std::size_t{0};
    auto reporter_b  = [&]<typename J>(J&&) { total_found += 1; };

    vault::amac::executor<Fanout>(intermediate_buffer, ctx_b, reporter_b);
    benchmark::DoNotOptimize(total_found);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

template <std::size_t Fanout, typename TransProbPolicy, typename StepRatioPolicy>
void bm_composite_pipeline(benchmark::State& state, std::size_t size_a, std::size_t size_b) {
  auto const       num_queries = static_cast<std::size_t>(state.range(0));
  constexpr double trans_prob  = static_cast<double>(TransProbPolicy::num) / TransProbPolicy::den;
  auto             data        = generate_data(num_queries, trans_prob, size_a, size_b);

  auto ctx_a = lower_bound_context{&data.stage1_data};
  auto ctx_b = upper_bound_context{&data.stage2_data};

  using composed_t = vault::amac::composed_context<
    lower_bound_context,
    upper_bound_context,
    lower_to_upper_transition,
    1,
    1,
    StepRatioPolicy,
    TransProbPolicy>;

  auto composed = composed_t{.ctx_a = ctx_a, .ctx_b = ctx_b, .transition = lower_to_upper_transition{}};

  for (auto _ : state) {
    auto total_found = std::size_t{0};
    auto reporter    = [&]<typename J>(J&&) {
      if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
        benchmark::DoNotOptimize(total_found += 1);
      }
    };
    vault::amac::pipeline_executor<Fanout, 4>(data.queries, composed, reporter);
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

template <std::size_t Fanout, std::size_t MaxIntermediateBytes, typename TransProbPolicy, typename StepRatioPolicy>
void bm_chunked_pipeline(benchmark::State& state, std::size_t size_a, std::size_t size_b) {
  auto const       num_queries = static_cast<std::size_t>(state.range(0));
  constexpr double trans_prob  = static_cast<double>(TransProbPolicy::num) / TransProbPolicy::den;
  auto             data        = generate_data(num_queries, trans_prob, size_a, size_b);

  auto ctx_a = lower_bound_context{&data.stage1_data};
  auto ctx_b = upper_bound_context{&data.stage2_data};

  using composed_t = vault::amac::composed_context<
    lower_bound_context,
    upper_bound_context,
    lower_to_upper_transition,
    1,
    1,
    StepRatioPolicy,
    TransProbPolicy>;

  auto composed = composed_t{.ctx_a = ctx_a, .ctx_b = ctx_b, .transition = lower_to_upper_transition{}};

  for (auto _ : state) {
    auto total_found = std::size_t{0};
    auto reporter    = [&]<typename J>(J&&) {
      if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
        benchmark::DoNotOptimize(total_found += 1);
      }
    };
    vault::amac::chunked_pipeline_executor<Fanout, MaxIntermediateBytes>(data.queries, composed, reporter);
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

// ----------------------------------------------------------------------------
// 4. The 3-Stage Benchmarks
// ----------------------------------------------------------------------------
template <std::size_t Fanout>
void bm_sequential_3_stage(
  benchmark::State& state,
  double            trans_prob,
  std::size_t       size_a,
  std::size_t       size_b,
  std::size_t       size_c
) {
  auto const num_queries = static_cast<std::size_t>(state.range(0));
  auto       data        = generate_data(num_queries, trans_prob, size_a, size_b, size_c);

  auto ctx_a = lower_bound_context{&data.stage1_data};
  auto ctx_b = upper_bound_context{&data.stage2_data};
  auto ctx_c = point_lookup_context{&data.stage3_data};

  auto trans_ab = lower_to_upper_transition{};
  auto trans_bc = upper_to_point_transition{};

  for (auto _ : state) {
    auto buffer_b = std::vector<upper_bound_job>{};
    buffer_b.reserve(num_queries);

    auto reporter_a = [&]<typename J>(J&& job) {
      if (auto opt_b = trans_ab(ctx_a, ctx_b, job)) {
        buffer_b.push_back(std::move(*opt_b));
      }
    };
    vault::amac::executor<Fanout>(data.queries, ctx_a, reporter_a);
    benchmark::DoNotOptimize(buffer_b.data());

    auto buffer_c = std::vector<point_lookup_job>{};
    buffer_c.reserve(buffer_b.size());

    auto reporter_b = [&]<typename J>(J&& job) {
      if (auto opt_c = trans_bc(ctx_b, ctx_c, job)) {
        buffer_c.push_back(std::move(*opt_c));
      }
    };
    vault::amac::executor<Fanout>(buffer_b, ctx_b, reporter_b);
    benchmark::DoNotOptimize(buffer_c.data());

    auto total_found = std::size_t{0};
    auto reporter_c  = [&]<typename J>(J&&) { total_found += 1; };

    vault::amac::executor<Fanout>(buffer_c, ctx_c, reporter_c);
    benchmark::DoNotOptimize(total_found);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

template <std::size_t Fanout, std::size_t MaxIntermediateBytes, typename TransProbPolicy, typename StepRatioPolicy>
void bm_chunked_3_stage(benchmark::State& state, std::size_t size_a, std::size_t size_b, std::size_t size_c) {
  auto const       num_queries = static_cast<std::size_t>(state.range(0));
  constexpr double trans_prob  = static_cast<double>(TransProbPolicy::num) / TransProbPolicy::den;
  auto             data        = generate_data(num_queries, trans_prob, size_a, size_b, size_c);

  auto ctx_a = lower_bound_context{&data.stage1_data};
  auto ctx_b = upper_bound_context{&data.stage2_data};
  auto ctx_c = point_lookup_context{&data.stage3_data};

  using composed_bc_t = vault::amac::composed_context<
    upper_bound_context,
    point_lookup_context,
    upper_to_point_transition,
    1,
    1,
    StepRatioPolicy,
    TransProbPolicy>;

  using composed_abc_t = vault::amac::composed_context<
    lower_bound_context,
    composed_bc_t,
    lower_to_upper_transition,
    1,
    1,
    StepRatioPolicy,
    TransProbPolicy>;

  auto composed_bc  = composed_bc_t{.ctx_a = ctx_b, .ctx_b = ctx_c, .transition = upper_to_point_transition{}};
  auto composed_abc = composed_abc_t{.ctx_a = ctx_a, .ctx_b = composed_bc, .transition = lower_to_upper_transition{}};

  for (auto _ : state) {
    auto total_found = std::size_t{0};
    auto reporter    = [&]<typename J>(J&&) {
      if constexpr (std::is_same_v<std::remove_cvref_t<J>, point_lookup_job>) {
        benchmark::DoNotOptimize(total_found += 1);
      }
    };
    vault::amac::chunked_pipeline_executor<Fanout, MaxIntermediateBytes>(data.queries, composed_abc, reporter);
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

// ----------------------------------------------------------------------------
// 5. Explicit Benchmark Registration
// ----------------------------------------------------------------------------
void register_benchmarks() {
  auto const min_batch = 10'000;
  auto const max_batch = 100'000'000;

  // Targeting 256 KB L2 cache to prevent thrashing
  constexpr std::size_t l2_cache_target = 262144;

  // Scenario 1: Balanced Steps (1:1 Ratio), 100% Transition Probability
  {
    using Prob100    = std::ratio<1, 1>;
    using Step1_1    = std::ratio<1, 1>;
    std::size_t size = 65536;

    benchmark::RegisterBenchmark("Sequential_Fanout16_Prob100_Step1:1", bm_sequential_amac<16>, 1.0, size, size)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark(
      "Composite_Fanout16_Prob100_Step1:1", bm_composite_pipeline<16, Prob100, Step1_1>, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark(
      "Chunked_Fanout16_Prob100_Step1:1", bm_chunked_pipeline<16, l2_cache_target, Prob100, Step1_1>, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
  }

  // Scenario 2: Heavy Filter (1/64 Transition Probability), Step Ratio 1:2
  {
    using Prob1_64     = std::ratio<1, 64>;
    using Step1_2      = std::ratio<1, 2>;
    std::size_t size_a = 4096;
    std::size_t size_b = 16'777'216;

    benchmark::RegisterBenchmark(
      "Sequential_Fanout16_Prob1_64_Step1:2", bm_sequential_amac<16>, 1.0 / 64.0, size_a, size_b
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark(
      "Composite_Fanout16_Prob1_64_Step1:2", bm_composite_pipeline<16, Prob1_64, Step1_2>, size_a, size_b
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark(
      "Chunked_Fanout16_Prob1_64_Step1:2", bm_chunked_pipeline<16, l2_cache_target, Prob1_64, Step1_2>, size_a, size_b
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
  }

  // Scenario 3: Varying Fanout Limits (2, 64) for the 100% Transition workload
  {
    using Prob100    = std::ratio<1, 1>;
    using Step1_1    = std::ratio<1, 1>;
    std::size_t size = 65536;

    benchmark::RegisterBenchmark(
      "Composite_Fanout2_Prob100_Step1:1", bm_composite_pipeline<2, Prob100, Step1_1>, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark(
      "Chunked_Fanout2_Prob100_Step1:1", bm_chunked_pipeline<2, l2_cache_target, Prob100, Step1_1>, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark(
      "Composite_Fanout64_Prob100_Step1:1", bm_composite_pipeline<64, Prob100, Step1_1>, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark(
      "Chunked_Fanout64_Prob100_Step1:1", bm_chunked_pipeline<64, l2_cache_target, Prob100, Step1_1>, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
  }

  // Scenario 4: The 3-Stage Pipelines
  {
    using Prob100    = std::ratio<1, 1>;
    using Step1_1    = std::ratio<1, 1>;
    std::size_t size = 65536;

    benchmark::RegisterBenchmark("Sequential_3Stage_Fanout16_Prob100", bm_sequential_3_stage<16>, 1.0, size, size, size)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch/10);
    benchmark::RegisterBenchmark(
      "Chunked_3Stage_Fanout16_Prob100", bm_chunked_3_stage<16, l2_cache_target, Prob100, Step1_1>, size, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch/10);
  }
}

int main(int argc, char** argv) {
  register_benchmarks();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
