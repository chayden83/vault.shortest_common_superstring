#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <vault/algorithm/amac_chunked.hpp>
#include <vault/algorithm/amac_pipeline.hpp>

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
      // Exact match found, clamp iterators to terminate
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

  auto composed = vault::amac::make_pipeline(
    ctx_a, vault::amac::make_edge<std::ratio<1, 1>, std::ratio<1, 1>>(lower_to_upper_transition{}), ctx_b
  );

  auto jobs = std::vector<lower_bound_job>{};
  jobs.reserve(num_elements);
  for (auto i = 0; i < num_elements; ++i) {
    jobs.push_back(lower_bound_job{.target_key = i, .low = 0, .high = 0, .mid = 0});
  }

  auto pipeline_results = std::vector<std::pair<int, std::size_t>>{};
  pipeline_results.reserve(num_elements);

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
        pipeline_results.emplace_back(job.original_key, job.low);
      }
    } else if constexpr (std::is_same_v<Tag, vault::amac::failed_tag>) {
      FAIL("Jobs should not fail in this test.");
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

  // Build the entire 3-stage pipeline efficiently using the new variadic factory
  auto composed_1_2_3 = vault::amac::make_pipeline(
    ctx_1,
    vault::amac::make_edge<std::ratio<1, 1>, std::ratio<1, 1>>(lower_to_upper_transition{}),
    ctx_2,
    vault::amac::make_edge<std::ratio<1, 1>, std::ratio<1, 1>>(upper_to_point_transition{}),
    ctx_3
  );

  auto jobs = std::vector<lower_bound_job>{};
  jobs.reserve(num_elements);
  for (auto i = 0; i < num_elements; ++i) {
    jobs.push_back(lower_bound_job{.target_key = i, .low = 0, .high = 0, .mid = 0});
  }

  auto final_results = std::vector<std::pair<int, int>>{};
  final_results.reserve(num_elements);

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      if constexpr (std::is_same_v<std::remove_cvref_t<J>, point_lookup_job>) {
        if (job.low == job.high && job.low < ctx_3.data->size()) {
          final_results.emplace_back(job.original_key, (*ctx_3.data)[job.low].second);
        }
      }
    } else if constexpr (std::is_same_v<Tag, vault::amac::failed_tag>) {
      FAIL("Jobs should not fail in this test.");
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
