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

  [[nodiscard]] auto finalize(lower_bound_job const& job) const noexcept -> std::expected<std::optional<int>, int> {
    if (job.low < data->size() && (*data)[job.low].first == job.target_key) {
      return std::optional<int>{(*data)[job.low].second};
    }
    return std::nullopt;
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

  [[nodiscard]] auto finalize(upper_bound_job const& job) const noexcept -> std::expected<std::optional<int>, int> {
    // Return the mapped value found at the preceding index (since upper_bound is strictly >)
    if (job.low > 0 && job.low <= data->size()) {
      return std::optional<int>{(*data)[job.low - 1].second};
    }
    return std::nullopt;
  }
};

// ----------------------------------------------------------------------------
// Stage 3: Point Lookup State Machine
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
// Transition Edges
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
// Test Cases
// ----------------------------------------------------------------------------
TEST_CASE("Chunked AMAC Executor processes large inputs seamlessly (2 Stages)", "[amac][chunked_pipeline]") {
  auto       stage1_data  = std::vector<std::pair<int, int>>{};
  auto       stage2_data  = std::vector<std::pair<int, int>>{};
  auto const num_elements = 50'000;

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
  for (auto i = 0; i < num_elements; ++i) {
    jobs.push_back({i, 0, 0, 0});
  }

  auto pipeline_results = std::vector<std::pair<int, int>>{};

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
        auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
        pipeline_results.emplace_back(job.original_key, payload);
      }
    }
  };

  vault::amac::chunked_pipeline_executor<16, 1024>(jobs, composed, reporter);

  REQUIRE(pipeline_results.size() == static_cast<std::size_t>(num_elements));
  std::ranges::sort(pipeline_results);

  for (auto i = 0; i < num_elements; ++i) {
    REQUIRE(pipeline_results[i].first == i);
    // Stage 1 output i*10. Stage 2 finds i*10 and maps to i*100.
    REQUIRE(pipeline_results[i].second == i * 100);
  }
}

TEST_CASE("Chunked AMAC Executor flawlessly recurses right-leaning 3-stage trees", "[amac][chunked_pipeline]") {
  auto       stage1_data  = std::vector<std::pair<int, int>>{};
  auto       stage2_data  = std::vector<std::pair<int, int>>{};
  auto       stage3_data  = std::vector<std::pair<int, int>>{};
  auto const num_elements = 50'000;

  for (auto i = 0; i < num_elements; ++i) {
    stage1_data.emplace_back(i, i * 10);
    stage2_data.emplace_back(i * 10, i * 100);
    stage3_data.emplace_back(i * 100, i * 1000);
  }

  auto ctx_1 = lower_bound_context{&stage1_data};
  auto ctx_2 = upper_bound_context{&stage2_data};
  auto ctx_3 = point_lookup_context{&stage3_data};

  auto composed_1_2_3 = vault::amac::make_pipeline(
    ctx_1,
    vault::amac::make_edge<std::ratio<1, 1>, std::ratio<1, 1>>(lower_to_upper_transition{}),
    ctx_2,
    vault::amac::make_edge<std::ratio<1, 1>, std::ratio<1, 1>>(upper_to_point_transition{}),
    ctx_3
  );

  auto jobs = std::vector<lower_bound_job>{};
  for (auto i = 0; i < num_elements; ++i) {
    jobs.push_back({i, 0, 0, 0});
  }

  auto final_results = std::vector<std::pair<int, int>>{};

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      if constexpr (std::is_same_v<std::remove_cvref_t<J>, point_lookup_job>) {
        auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
        final_results.emplace_back(job.original_key, payload);
      }
    }
  };

  vault::amac::chunked_pipeline_executor<16, 1024>(jobs, composed_1_2_3, reporter);

  REQUIRE(final_results.size() == static_cast<std::size_t>(num_elements));
  std::ranges::sort(final_results);

  for (auto i = 0; i < num_elements; ++i) {
    REQUIRE(final_results[i].first == i);
    REQUIRE(final_results[i].second == i * 1000);
  }
}
