#include <benchmark/benchmark.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
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

  [[nodiscard]] auto init(lower_bound_job& job) const noexcept -> std::expected<vault::amac::step_result<1>, int> {
    job.low  = 0;
    job.high = data->size();
    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return vault::amac::step_result<1>{&(*data)[job.mid]};
    }
    return vault::amac::step_result<1>{nullptr};
  }

  [[nodiscard]] auto step(lower_bound_job& job) const noexcept -> std::expected<vault::amac::step_result<1>, int> {
    if (job.low >= job.high) {
      return vault::amac::step_result<1>{nullptr};
    }

    if ((*data)[job.mid].first < job.target_key) {
      job.low = job.mid + 1;
    } else {
      job.high = job.mid;
    }

    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return vault::amac::step_result<1>{&(*data)[job.mid]};
    }
    return vault::amac::step_result<1>{nullptr};
  }

  [[nodiscard]] auto finalize(lower_bound_job const& job) const noexcept -> std::expected<std::optional<int>, int> {
    if (job.low < data->size() && (*data)[job.low].first == job.target_key) {
      return std::optional<int>{(*data)[job.low].second};
    }
    return std::nullopt;
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

  [[nodiscard]] auto init(upper_bound_job& job) const noexcept -> std::expected<vault::amac::step_result<1>, int> {
    job.low  = 0;
    job.high = data->size();
    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return vault::amac::step_result<1>{&(*data)[job.mid]};
    }
    return vault::amac::step_result<1>{nullptr};
  }

  [[nodiscard]] auto step(upper_bound_job& job) const noexcept -> std::expected<vault::amac::step_result<1>, int> {
    if (job.low >= job.high) {
      return vault::amac::step_result<1>{nullptr};
    }

    if ((*data)[job.mid].first <= job.target_key) {
      job.low = job.mid + 1;
    } else {
      job.high = job.mid;
    }

    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return vault::amac::step_result<1>{&(*data)[job.mid]};
    }
    return vault::amac::step_result<1>{nullptr};
  }

  [[nodiscard]] auto finalize(upper_bound_job const& job) const noexcept -> std::expected<std::optional<int>, int> {
    if (job.low > 0 && job.low <= data->size()) {
      return std::optional<int>{(*data)[job.low - 1].second};
    }
    return std::nullopt;
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

  [[nodiscard]] auto init(point_lookup_job& job) const noexcept -> std::expected<vault::amac::step_result<1>, int> {
    job.low  = 0;
    job.high = data->size();
    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return vault::amac::step_result<1>{&(*data)[job.mid]};
    }
    return vault::amac::step_result<1>{nullptr};
  }

  [[nodiscard]] auto step(point_lookup_job& job) const noexcept -> std::expected<vault::amac::step_result<1>, int> {
    if (job.low >= job.high) {
      return vault::amac::step_result<1>{nullptr};
    }

    if ((*data)[job.mid].first < job.target_key) {
      job.low = job.mid + 1;
    } else if ((*data)[job.mid].first > job.target_key) {
      job.high = job.mid;
    } else {
      job.low  = job.mid;
      job.high = job.mid;
      return vault::amac::step_result<1>{nullptr};
    }

    if (job.low < job.high) {
      job.mid = job.low + (job.high - job.low) / 2;
      return vault::amac::step_result<1>{&(*data)[job.mid]};
    }
    return vault::amac::step_result<1>{nullptr};
  }

  [[nodiscard]] auto finalize(point_lookup_job const& job) const noexcept -> std::expected<std::optional<int>, int> {
    if (job.low < data->size() && (*data)[job.low].first == job.target_key) {
      return std::optional<int>{(*data)[job.low].second};
    }
    return std::nullopt;
  }
};

// ----------------------------------------------------------------------------
// Transition Edges: Pure Mappings
// ----------------------------------------------------------------------------
struct lower_to_upper_transition {
  auto operator()(lower_bound_job const& job_a, int payload_a) const -> std::optional<upper_bound_job> {
    return upper_bound_job{.original_key = job_a.target_key, .target_key = payload_a, .low = 0, .high = 0, .mid = 0};
  }
};

struct upper_to_point_transition {
  auto operator()(upper_bound_job const& job_a, int payload_a) const -> std::optional<point_lookup_job> {
    return point_lookup_job{.original_key = job_a.original_key, .target_key = payload_a, .low = 0, .high = 0, .mid = 0};
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
      data.queries.push_back(lower_bound_job{data.stage1_data[i % size_a].first, 0, 0, 0});
    } else {
      data.queries.push_back(lower_bound_job{data.stage1_data[i % size_a].first + 1, 0, 0, 0});
    }
  }
  return data;
}

// ----------------------------------------------------------------------------
// 3. Pure STD Serial Baselines
// ----------------------------------------------------------------------------
template <typename TransProbPolicy>
void bm_std_serial_2_stage(benchmark::State& state, std::size_t size_a, std::size_t size_b) {
  auto const       num_queries = static_cast<std::size_t>(state.range(0));
  constexpr double trans_prob  = static_cast<double>(TransProbPolicy::num) / TransProbPolicy::den;
  auto             data        = generate_data(num_queries, trans_prob, size_a, size_b);
  for (auto _ : state) {
    auto total_found = std::size_t{0};
    for (auto const& q : data.queries) {
      auto it1 = std::ranges::lower_bound(data.stage1_data, q.target_key, {}, &std::pair<int, int>::first);
      if (it1 != data.stage1_data.end() && it1->first == q.target_key) {
        auto it2 = std::ranges::upper_bound(data.stage2_data, it1->second, {}, &std::pair<int, int>::first);
        benchmark::DoNotOptimize(total_found += 1);
        benchmark::DoNotOptimize(it2);
      }
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

template <typename TransProbPolicy>
void bm_std_queued_2_stage(benchmark::State& state, std::size_t size_a, std::size_t size_b) {
  auto const       num_queries = static_cast<std::size_t>(state.range(0));
  constexpr double trans_prob  = static_cast<double>(TransProbPolicy::num) / TransProbPolicy::den;
  auto             data        = generate_data(num_queries, trans_prob, size_a, size_b);
  for (auto _ : state) {
    auto intermediate = std::vector<int>{};
    intermediate.reserve(num_queries);
    for (auto const& q : data.queries) {
      auto it1 = std::ranges::lower_bound(data.stage1_data, q.target_key, {}, &std::pair<int, int>::first);
      if (it1 != data.stage1_data.end() && it1->first == q.target_key) {
        intermediate.push_back(it1->second);
      }
    }
    benchmark::DoNotOptimize(intermediate.data());
    auto total_found = std::size_t{0};
    for (auto const target2 : intermediate) {
      auto it2 = std::ranges::upper_bound(data.stage2_data, target2, {}, &std::pair<int, int>::first);
      benchmark::DoNotOptimize(total_found += 1);
      benchmark::DoNotOptimize(it2);
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

template <typename TransProbPolicy>
void bm_std_serial_3_stage(benchmark::State& state, std::size_t size_a, std::size_t size_b, std::size_t size_c) {
  auto const       num_queries = static_cast<std::size_t>(state.range(0));
  constexpr double trans_prob  = static_cast<double>(TransProbPolicy::num) / TransProbPolicy::den;
  auto             data        = generate_data(num_queries, trans_prob, size_a, size_b, size_c);
  for (auto _ : state) {
    auto total_found = std::size_t{0};
    for (auto const& q : data.queries) {
      auto it1 = std::ranges::lower_bound(data.stage1_data, q.target_key, {}, &std::pair<int, int>::first);
      if (it1 != data.stage1_data.end() && it1->first == q.target_key) {
        auto it2 = std::ranges::upper_bound(data.stage2_data, it1->second, {}, &std::pair<int, int>::first);
        if (it2 != data.stage2_data.begin()) {
          auto target3 = std::prev(it2)->second;
          auto it3     = std::ranges::lower_bound(data.stage3_data, target3, {}, &std::pair<int, int>::first);
          if (it3 != data.stage3_data.end() && it3->first == target3) {
            benchmark::DoNotOptimize(total_found += 1);
          }
        }
      }
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

template <typename TransProbPolicy>
void bm_std_queued_3_stage(benchmark::State& state, std::size_t size_a, std::size_t size_b, std::size_t size_c) {
  auto const       num_queries = static_cast<std::size_t>(state.range(0));
  constexpr double trans_prob  = static_cast<double>(TransProbPolicy::num) / TransProbPolicy::den;
  auto             data        = generate_data(num_queries, trans_prob, size_a, size_b, size_c);
  for (auto _ : state) {
    auto buffer_b = std::vector<int>{};
    buffer_b.reserve(num_queries);
    for (auto const& q : data.queries) {
      auto it1 = std::ranges::lower_bound(data.stage1_data, q.target_key, {}, &std::pair<int, int>::first);
      if (it1 != data.stage1_data.end() && it1->first == q.target_key) {
        buffer_b.push_back(it1->second);
      }
    }
    benchmark::DoNotOptimize(buffer_b.data());
    auto buffer_c = std::vector<int>{};
    buffer_c.reserve(buffer_b.size());
    for (auto const target2 : buffer_b) {
      auto it2 = std::ranges::upper_bound(data.stage2_data, target2, {}, &std::pair<int, int>::first);
      if (it2 != data.stage2_data.begin()) {
        buffer_c.push_back(std::prev(it2)->second);
      }
    }
    benchmark::DoNotOptimize(buffer_c.data());
    auto total_found = std::size_t{0};
    for (auto const target3 : buffer_c) {
      auto it3 = std::ranges::lower_bound(data.stage3_data, target3, {}, &std::pair<int, int>::first);
      if (it3 != data.stage3_data.end() && it3->first == target3) {
        benchmark::DoNotOptimize(total_found += 1);
      }
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

// ----------------------------------------------------------------------------
// 4. The AMAC Benchmarks
// ----------------------------------------------------------------------------
template <std::size_t Fanout>
void bm_sequential_amac(benchmark::State& state, double trans_prob, std::size_t size_a, std::size_t size_b) {
  auto const num_queries = static_cast<std::size_t>(state.range(0));
  auto       data        = generate_data(num_queries, trans_prob, size_a, size_b);
  auto       ctx_a       = lower_bound_context{&data.stage1_data};
  auto       ctx_b       = upper_bound_context{&data.stage2_data};
  auto       transition  = lower_to_upper_transition{};

  for (auto _ : state) {
    auto intermediate_buffer = std::vector<upper_bound_job>{};
    intermediate_buffer.reserve(num_queries);
    auto reporter_a = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&& job, Args&&... args) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
        if (auto opt_b = transition(job, payload)) {
          intermediate_buffer.push_back(std::move(*opt_b));
        }
      }
    };
    vault::amac::executor<Fanout>(data.queries, ctx_a, reporter_a);
    benchmark::DoNotOptimize(intermediate_buffer.data());
    auto total_found = std::size_t{0};
    auto reporter_b  = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&&, Args&&...) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        total_found += 1;
      }
    };
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
  auto             ctx_a       = lower_bound_context{&data.stage1_data};
  auto             ctx_b       = upper_bound_context{&data.stage2_data};
  auto             composed    = vault::amac::make_pipeline(
    ctx_a, vault::amac::make_edge<StepRatioPolicy, TransProbPolicy>(lower_to_upper_transition{}), ctx_b
  );

  for (auto _ : state) {
    auto total_found = std::size_t{0};
    auto reporter    = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&&, Args&&...) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
          benchmark::DoNotOptimize(total_found += 1);
        }
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
  auto             ctx_a       = lower_bound_context{&data.stage1_data};
  auto             ctx_b       = upper_bound_context{&data.stage2_data};
  auto             composed    = vault::amac::make_pipeline(
    ctx_a, vault::amac::make_edge<StepRatioPolicy, TransProbPolicy>(lower_to_upper_transition{}), ctx_b
  );

  for (auto _ : state) {
    auto total_found = std::size_t{0};
    auto reporter    = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&&, Args&&...) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
          benchmark::DoNotOptimize(total_found += 1);
        }
      }
    };
    vault::amac::chunked_pipeline_executor<Fanout, MaxIntermediateBytes>(data.queries, composed, reporter);
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

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
  auto       ctx_a       = lower_bound_context{&data.stage1_data};
  auto       ctx_b       = upper_bound_context{&data.stage2_data};
  auto       ctx_c       = point_lookup_context{&data.stage3_data};
  auto       trans_ab    = lower_to_upper_transition{};
  auto       trans_bc    = upper_to_point_transition{};

  for (auto _ : state) {
    auto buffer_b = std::vector<upper_bound_job>{};
    buffer_b.reserve(num_queries);
    auto reporter_a = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&& job, Args&&... args) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
        if (auto opt_b = trans_ab(job, payload)) {
          buffer_b.push_back(std::move(*opt_b));
        }
      }
    };
    vault::amac::executor<Fanout>(data.queries, ctx_a, reporter_a);
    benchmark::DoNotOptimize(buffer_b.data());
    auto buffer_c = std::vector<point_lookup_job>{};
    buffer_c.reserve(buffer_b.size());
    auto reporter_b = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&& job, Args&&... args) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
        if (auto opt_c = trans_bc(job, payload)) {
          buffer_c.push_back(std::move(*opt_c));
        }
      }
    };
    vault::amac::executor<Fanout>(buffer_b, ctx_b, reporter_b);
    benchmark::DoNotOptimize(buffer_c.data());
    auto total_found = std::size_t{0};
    auto reporter_c  = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&&, Args&&...) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        total_found += 1;
      }
    };
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
  auto             ctx_a       = lower_bound_context{&data.stage1_data};
  auto             ctx_b       = upper_bound_context{&data.stage2_data};
  auto             ctx_c       = point_lookup_context{&data.stage3_data};
  auto             composed    = vault::amac::make_pipeline(
    ctx_a,
    vault::amac::make_edge<StepRatioPolicy, TransProbPolicy>(lower_to_upper_transition{}),
    ctx_b,
    vault::amac::make_edge<StepRatioPolicy, TransProbPolicy>(upper_to_point_transition{}),
    ctx_c
  );

  for (auto _ : state) {
    auto total_found = std::size_t{0};
    auto reporter    = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&&, Args&&...) {
      if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<J>, point_lookup_job>) {
          benchmark::DoNotOptimize(total_found += 1);
        }
      }
    };
    vault::amac::chunked_pipeline_executor<Fanout, MaxIntermediateBytes>(data.queries, composed, reporter);
  }
  state.SetItemsProcessed(state.iterations() * num_queries);
}

// ----------------------------------------------------------------------------
// 5. Explicit Benchmark Registration
// ----------------------------------------------------------------------------
void register_benchmarks() {
  auto const            min_batch       = 10'000;
  auto const            max_batch       = 10'000'000;
  constexpr std::size_t l2_cache_target = 262144;

  {
    using Prob100    = std::ratio<1, 1>;
    using Step1_1    = std::ratio<1, 1>;
    std::size_t size = 65536;
    benchmark::RegisterBenchmark("Std_Serial_Prob100", bm_std_serial_2_stage<Prob100>, size, size)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark("Std_Queued_Prob100", bm_std_queued_2_stage<Prob100>, size, size)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
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

  {
    using Prob1_64     = std::ratio<1, 64>;
    using Step1_2      = std::ratio<1, 2>;
    std::size_t size_a = 4096;
    std::size_t size_b = 16'777'216;
    benchmark::RegisterBenchmark("Std_Serial_Prob1_64", bm_std_serial_2_stage<Prob1_64>, size_a, size_b)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
    benchmark::RegisterBenchmark("Std_Queued_Prob1_64", bm_std_queued_2_stage<Prob1_64>, size_a, size_b)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch);
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

  {
    using Prob100    = std::ratio<1, 1>;
    using Step1_1    = std::ratio<1, 1>;
    std::size_t size = 65536;
    benchmark::RegisterBenchmark("Std_Serial_3Stage_Prob100", bm_std_serial_3_stage<Prob100>, size, size, size)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch / 10)
      ->Iterations(1)
      ->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark("Std_Queued_3Stage_Prob100", bm_std_queued_3_stage<Prob100>, size, size, size)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch / 10)
      ->Iterations(1)
      ->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark("Sequential_3Stage_Fanout16_Prob100", bm_sequential_3_stage<16>, 1.0, size, size, size)
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch / 10)
      ->Iterations(1)
      ->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark(
      "Chunked_3Stage_Fanout16_Prob100", bm_chunked_3_stage<16, l2_cache_target, Prob100, Step1_1>, size, size, size
    )
      ->RangeMultiplier(10)
      ->Range(min_batch, max_batch / 10)
      ->Iterations(1)
      ->Unit(benchmark::kMillisecond);
  }
}

int main(int argc, char** argv) {
  register_benchmarks();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
