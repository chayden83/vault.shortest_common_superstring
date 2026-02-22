#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <vault/algorithm/amac_chunked.hpp>

// ----------------------------------------------------------------------------
// Stage 1: Lower Bound State Machine
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
    return 4;
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

// ----------------------------------------------------------------------------
// Stage 2: Upper Bound State Machine
// ----------------------------------------------------------------------------
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
    return 4;
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

// ----------------------------------------------------------------------------
// Stage 3: Point Lookup State Machine (for 3-stage testing)
// ----------------------------------------------------------------------------
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
    return 4;
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
      // Exact match found, clamp iterators to terminate
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

// ----------------------------------------------------------------------------
// Transition Edges
// ----------------------------------------------------------------------------
struct lower_to_upper_transition {
  // Templated CtxB so it safely ignores whether Stage B is a raw context or a nested composed_context
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

    // upper_bound finds the element strictly *greater* than the target.
    // The match we actually want to transition is at the preceding index.
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
// Test Cases
// ----------------------------------------------------------------------------
TEST_CASE("Chunked AMAC Executor processes large inputs seamlessly (2 Stages)", "[amac][chunked_pipeline]") {
  auto stage1_data = std::vector<std::pair<int, int>>{};
  auto stage2_data = std::vector<std::pair<int, int>>{};

  auto const num_elements = 50'000;
  stage1_data.reserve(num_elements);
  stage2_data.reserve(num_elements);

  for (auto i = 0; i < num_elements; ++i) {
    stage1_data.emplace_back(i, i * 10);
    stage2_data.emplace_back(i * 10, i * 100);
  }

  auto ctx_a = lower_bound_context{&stage1_data};
  auto ctx_b = upper_bound_context{&stage2_data};

  using composed_t = vault::amac::composed_context<
    lower_bound_context,
    upper_bound_context,
    lower_to_upper_transition,
    lower_bound_context::fanout(),
    upper_bound_context::fanout(),
    std::ratio<1, 1>,
    std::ratio<1, 1>>;

  auto composed = composed_t{.ctx_a = ctx_a, .ctx_b = ctx_b, .transition = lower_to_upper_transition{}};

  auto jobs = std::vector<lower_bound_job>{};
  jobs.reserve(num_elements);
  for (auto i = 0; i < num_elements; ++i) {
    jobs.push_back(lower_bound_job{.target_key = i, .low = 0, .high = 0, .mid = 0});
  }

  auto pipeline_results = std::vector<std::pair<int, std::size_t>>{};
  pipeline_results.reserve(num_elements);

  auto reporter = [&]<typename J>(J&& job) {
    if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
      pipeline_results.emplace_back(job.original_key, job.low);
    }
  };

  vault::amac::chunked_pipeline_executor<16, 1024>(jobs, composed, reporter);

  REQUIRE(pipeline_results.size() == static_cast<std::size_t>(num_elements));

  std::ranges::sort(pipeline_results, [](auto const& a, auto const& b) { return a.first < b.first; });

  for (auto i = 0; i < num_elements; ++i) {
    auto target1 = i;
    auto it1     = std::ranges::lower_bound(stage1_data, target1, {}, &std::pair<int, int>::first);
    REQUIRE(it1 != stage1_data.end());

    auto target2        = it1->second;
    auto it2            = std::ranges::upper_bound(stage2_data, target2, {}, &std::pair<int, int>::first);
    auto expected_index = static_cast<std::size_t>(std::distance(stage2_data.begin(), it2));

    REQUIRE(pipeline_results[i].first == target1);
    REQUIRE(pipeline_results[i].second == expected_index);
  }
}

TEST_CASE("Chunked AMAC Executor flawlessly recurses right-leaning 3-stage trees", "[amac][chunked_pipeline]") {
  auto stage1_data = std::vector<std::pair<int, int>>{};
  auto stage2_data = std::vector<std::pair<int, int>>{};
  auto stage3_data = std::vector<std::pair<int, int>>{};

  auto const num_elements = 50'000;
  stage1_data.reserve(num_elements);
  stage2_data.reserve(num_elements);
  stage3_data.reserve(num_elements);

  for (auto i = 0; i < num_elements; ++i) {
    stage1_data.emplace_back(i, i * 10);
    stage2_data.emplace_back(i * 10, i * 100);
    stage3_data.emplace_back(i * 100, i * 1000);
  }

  auto ctx_1 = lower_bound_context{&stage1_data};
  auto ctx_2 = upper_bound_context{&stage2_data};
  auto ctx_3 = point_lookup_context{&stage3_data};

  // 1. Compose Stage 2 and Stage 3 (The inner right node)
  using composed_2_3_t = vault::amac::composed_context<
    upper_bound_context,
    point_lookup_context,
    upper_to_point_transition,
    upper_bound_context::fanout(),
    point_lookup_context::fanout(),
    std::ratio<1, 1>,
    std::ratio<1, 1>>;

  auto composed_2_3 = composed_2_3_t{.ctx_a = ctx_2, .ctx_b = ctx_3, .transition = upper_to_point_transition{}};

  // 2. Compose Stage 1 with the composed (2+3) node
  using composed_1_2_3_t = vault::amac::composed_context<
    lower_bound_context,
    composed_2_3_t,
    lower_to_upper_transition,
    lower_bound_context::fanout(),
    composed_2_3_t::fanout(), // Pass the aggregated fanout up the tree
    std::ratio<1, 1>,
    std::ratio<1, 1>>;

  auto composed_1_2_3 =
    composed_1_2_3_t{.ctx_a = ctx_1, .ctx_b = composed_2_3, .transition = lower_to_upper_transition{}};

  auto jobs = std::vector<lower_bound_job>{};
  jobs.reserve(num_elements);
  for (auto i = 0; i < num_elements; ++i) {
    jobs.push_back(lower_bound_job{.target_key = i, .low = 0, .high = 0, .mid = 0});
  }

  auto final_results = std::vector<std::pair<int, int>>{};
  final_results.reserve(num_elements);

  // The reporter now cleanly only receives the final Stage 3 job type
  auto reporter = [&]<typename J>(J&& job) {
    if constexpr (std::is_same_v<std::remove_cvref_t<J>, point_lookup_job>) {
      if (job.low == job.high && job.low < ctx_3.data->size()) {
        final_results.emplace_back(job.original_key, (*ctx_3.data)[job.low].second);
      }
    }
  };

  // Execute the entire 3-stage pipeline natively
  vault::amac::chunked_pipeline_executor<16, 1024>(jobs, composed_1_2_3, reporter);

  REQUIRE(final_results.size() == static_cast<std::size_t>(num_elements));

  std::ranges::sort(final_results, [](auto const& a, auto const& b) { return a.first < b.first; });

  for (auto i = 0; i < num_elements; ++i) {
    REQUIRE(final_results[i].first == i);
    REQUIRE(final_results[i].second == i * 1000);
  }
}
