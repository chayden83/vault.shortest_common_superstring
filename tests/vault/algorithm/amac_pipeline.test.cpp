#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
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

// ----------------------------------------------------------------------------
// Stage 2: Upper Bound State Machine
// ----------------------------------------------------------------------------
struct upper_bound_job {
  int         original_key; // Preserved for test verification
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

  [[nodiscard]] auto finalize(upper_bound_job const& job) const noexcept
    -> std::expected<std::optional<std::size_t>, int> {
    // Return the index found as the payload
    return std::optional<std::size_t>{job.low};
  }
};

// ----------------------------------------------------------------------------
// Transition Edge: Pure Mapping Function
// ----------------------------------------------------------------------------
struct lower_to_upper_transition {
  auto operator()(lower_bound_job const& job_a, int const payload_a) const -> std::optional<upper_bound_job> {
    return upper_bound_job{.original_key = job_a.target_key, .target_key = payload_a, .low = 0, .high = 0, .mid = 0};
  }
};

// ----------------------------------------------------------------------------
// Test Cases
// ----------------------------------------------------------------------------
TEST_CASE("AMAC Pipeline handles pathological backpressure without data loss", "[amac][pipeline]") {
  auto stage1_data = std::vector<std::pair<int, int>>{};
  auto stage2_data = std::vector<std::pair<int, int>>{};

  auto const num_elements = 100'000;
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
        auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
        pipeline_results.emplace_back(job.original_key, payload);
      }
    } else if constexpr (std::is_same_v<Tag, vault::amac::failed_tag>) {
      FAIL("Jobs should not fail in this backpressure test.");
    }
  };

  vault::amac::pipeline_executor<16, 1>(jobs, composed, reporter);

  REQUIRE(pipeline_results.size() == static_cast<std::size_t>(num_elements));

  std::ranges::sort(pipeline_results, [](auto const& a, auto const& b) { return a.first < b.first; });

  for (auto i = 0; i < num_elements; ++i) {
    auto target1 = i;

    auto it1 = std::ranges::lower_bound(stage1_data, target1, {}, &std::pair<int, int>::first);
    REQUIRE(it1 != stage1_data.end());

    auto target2        = it1->second;
    auto it2            = std::ranges::upper_bound(stage2_data, target2, {}, &std::pair<int, int>::first);
    auto expected_index = static_cast<std::size_t>(std::distance(stage2_data.begin(), it2));

    REQUIRE(pipeline_results[i].first == target1);
    REQUIRE(pipeline_results[i].second == expected_index);
  }
}
