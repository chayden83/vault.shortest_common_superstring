#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
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
// Transition Edge
// ----------------------------------------------------------------------------
struct lower_to_upper_transition {
  template <typename CtxB>
  auto operator()(lower_bound_context const& ctx_a, CtxB const& /* ctx_b */, lower_bound_job const& job_a) const
    -> std::optional<upper_bound_job> {

    // Only transition if the lower_bound found an exact match
    if (job_a.low < ctx_a.data->size() && (*ctx_a.data)[job_a.low].first == job_a.target_key) {
      auto mapped_value = (*ctx_a.data)[job_a.low].second;
      return upper_bound_job{
        .original_key = job_a.target_key, .target_key = mapped_value, .low = 0, .high = 0, .mid = 0
      };
    }
    return std::nullopt;
  }
};

// ----------------------------------------------------------------------------
// Test Cases
// ----------------------------------------------------------------------------
TEST_CASE("AMAC Pipeline handles pathological backpressure without data loss", "[amac][pipeline]") {
  // 1. Generate massive, tightly packed test data
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

  // 2. Define the composed context using the declarative factory.
  // We explicitly claim 100% transition probability (1/1) and an equal step ratio (1/1)
  auto composed = vault::amac::make_pipeline(
    ctx_a, vault::amac::make_edge<std::ratio<1, 1>, std::ratio<1, 1>>(lower_to_upper_transition{}), ctx_b
  );

  // 3. Create a massive pathological input batch where every single job transitions
  auto jobs = std::vector<lower_bound_job>{};
  jobs.reserve(num_elements);
  for (auto i = 0; i < num_elements; ++i) {
    jobs.push_back(lower_bound_job{.target_key = i, .low = 0, .high = 0, .mid = 0});
  }

  // 4. Execute the pipeline with an artificially tiny buffer (BufferMultiplier = 1)
  // This absolutely guarantees massive buffer overflow events and constant backpressure stalling.
  auto pipeline_results = std::vector<std::pair<int, std::size_t>>{};
  pipeline_results.reserve(num_elements);

  auto reporter = [&]<typename J>(J&& job) {
    if constexpr (std::is_same_v<std::remove_cvref_t<J>, upper_bound_job>) {
      pipeline_results.emplace_back(job.original_key, job.low);
    }
  };

  vault::amac::pipeline_executor<16, 1>(jobs, composed, reporter);

  // 5. Verify correctness against standard library execution
  REQUIRE(pipeline_results.size() == static_cast<std::size_t>(num_elements));

  // Sort results since AMAC execution order is fundamentally non-deterministic due to MLP
  std::ranges::sort(pipeline_results, [](auto const& a, auto const& b) { return a.first < b.first; });

  for (auto i = 0; i < num_elements; ++i) {
    auto target1 = i;

    // Standard library baseline
    auto it1 = std::ranges::lower_bound(stage1_data, target1, {}, &std::pair<int, int>::first);
    REQUIRE(it1 != stage1_data.end());

    auto target2        = it1->second;
    auto it2            = std::ranges::upper_bound(stage2_data, target2, {}, &std::pair<int, int>::first);
    auto expected_index = static_cast<std::size_t>(std::distance(stage2_data.begin(), it2));

    REQUIRE(pipeline_results[i].first == target1);
    REQUIRE(pipeline_results[i].second == expected_index);
  }
}
