#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <expected>
#include <memory>
#include <optional>
#include <random>
#include <ranges>
#include <vector>

// --- Test Job Context Definition ---

static constexpr inline struct JobContext {
  [[nodiscard]] static constexpr uint64_t fanout() {
    return 1;
  }

  auto init(auto& job) const noexcept -> decltype(job.init()) {
    return job.init();
  }

  auto step(auto& job) const noexcept -> decltype(job.step()) {
    return job.step();
  }

  auto finalize(auto& job) const noexcept -> decltype(job.finalize()) {
    return job.finalize();
  }
} job_context{};

// --- Test Jobs ---

// A synthetic job that counts down from a specific number.
// Used to verify that AMAC runs every job to completion.
class CountdownJob {
  int m_counter;

public:
  using Needle = int;

  explicit CountdownJob(int start_count)
    : m_counter(start_count) {}

  [[nodiscard]] std::expected<vault::amac::step_result<1>, int> init() noexcept {
    if (m_counter <= 0) {
      return vault::amac::step_result<1>{nullptr};
    }
    m_counter--;
    return vault::amac::step_result<1>{this};
  }

  [[nodiscard]] std::expected<vault::amac::step_result<1>, int> step() noexcept {
    return init();
  }

  // The Payload for a countdown is the final value (always 0 on success)
  [[nodiscard]] std::expected<std::optional<int>, int> finalize() noexcept {
    return std::optional<int>{m_counter};
  }

  [[nodiscard]] int counter() const {
    return m_counter;
  }
};

// Verify Concept Compliance
static_assert(vault::amac::concepts::job_context<JobContext, CountdownJob>);

// A synthetic job that intentionally fails at a specific step to test error routing.
class FailingJob {
  int m_counter;
  int m_fail_at;

public:
  explicit FailingJob(int start_count, int fail_at)
    : m_counter(start_count)
    , m_fail_at(fail_at) {}

  [[nodiscard]] std::expected<vault::amac::step_result<1>, int> init() noexcept {
    if (m_counter == m_fail_at) {
      return std::unexpected(404);
    }
    if (m_counter <= 0) {
      return vault::amac::step_result<1>{nullptr};
    }
    m_counter--;
    return vault::amac::step_result<1>{this};
  }

  [[nodiscard]] std::expected<vault::amac::step_result<1>, int> step() noexcept {
    return init();
  }

  [[nodiscard]] std::expected<std::optional<int>, int> finalize() noexcept {
    if (m_counter == m_fail_at) {
      return std::unexpected(404);
    }
    return std::optional<int>{m_counter};
  }

  [[nodiscard]] int counter() const {
    return m_counter;
  }
};

// --- Test Suite ---

TEST_CASE("AMAC Executor: Countdown Integrity", "[amac][executor]") {
  const size_t num_jobs        = GENERATE(0, 1, 15, 16, 17, 100, 1000);
  const int    max_start_count = GENERATE(0, 1, 5, 10);

  std::vector<int> start_counts;
  start_counts.reserve(num_jobs);

  std::mt19937                       rng(42);
  std::uniform_int_distribution<int> dist(0, max_start_count);

  for (size_t i = 0; i < num_jobs; ++i) {
    start_counts.push_back(dist(rng));
  }

  size_t reported_count = 0;

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      reported_count++;
      // Unpack Payload
      auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
      REQUIRE(payload == 0);
      REQUIRE(job.counter() == 0);
    } else if constexpr (std::is_same_v<Tag, vault::amac::failed_tag>) {
      FAIL("CountdownJob should never fail.");
    }
  };

  auto jobs = start_counts | std::views::transform([](int count) { return CountdownJob(count); });

  vault::amac::executor<16>(jobs, job_context, reporter);

  CHECK(reported_count == num_jobs);
}

TEST_CASE("AMAC Executor: Error Routing via std::expected", "[amac][error_handling]") {
  const size_t num_jobs = 100;

  std::vector<std::pair<int, int>> configs;
  configs.reserve(num_jobs);

  for (size_t i = 0; i < num_jobs; ++i) {
    int start_count = 5;
    int fail_at     = (i % 2 == 0) ? -1 : 2;
    configs.emplace_back(start_count, fail_at);
  }

  size_t completed_count = 0;
  size_t failed_count    = 0;

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      completed_count++;
      auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
      REQUIRE(payload == 0);
    } else if constexpr (std::is_same_v<Tag, vault::amac::failed_tag>) {
      failed_count++;
      REQUIRE(job.counter() == 2);
      auto err = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
      REQUIRE(err == 404);
    }
  };

  auto jobs = configs | std::views::transform([](auto config) { return FailingJob(config.first, config.second); });

  vault::amac::executor<16>(jobs, job_context, reporter);

  CHECK(completed_count == 50);
  CHECK(failed_count == 50);
}

TEST_CASE("AMAC Executor: Batch Size Sensitivity", "[amac][batch_size]") {
  const size_t     num_jobs = 100;
  std::vector<int> start_counts(num_jobs, 5);

  size_t reported_count = 0;
  auto   reporter       = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      reported_count++;
      auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
      REQUIRE(payload == 0);
      REQUIRE(job.counter() == 0);
    }
  };

  auto jobs = start_counts | std::views::transform([](int count) { return CountdownJob(count); });

  SECTION("Batch Size = 1") {
    vault::amac::executor<1>(jobs, job_context, reporter);
    CHECK(reported_count == num_jobs);
  }

  SECTION("Batch Size = 2") {
    vault::amac::executor<2>(jobs, job_context, reporter);
    CHECK(reported_count == num_jobs);
  }
}

TEST_CASE("AMAC Executor: Double Free Regression Test", "[amac][resource][asan]") {
  class ResourceJob {
    std::unique_ptr<int> m_resource;
    int                  m_steps_remaining;

  public:
    using Needle = std::pair<int, int>;

    explicit ResourceJob(Needle n)
      : m_resource(std::make_unique<int>(n.first))
      , m_steps_remaining(n.second) {}

    ResourceJob(const ResourceJob&)            = delete;
    ResourceJob& operator=(const ResourceJob&) = delete;
    ResourceJob(ResourceJob&&)                 = default;
    ResourceJob& operator=(ResourceJob&&)      = default;

    [[nodiscard]] std::expected<vault::amac::step_result<1>, int> init() noexcept {
      if (m_steps_remaining <= 0) {
        return vault::amac::step_result<1>{nullptr};
      }
      m_steps_remaining--;
      return vault::amac::step_result<1>{m_resource.get()};
    }

    [[nodiscard]] std::expected<vault::amac::step_result<1>, int> step() noexcept {
      return init();
    }

    [[nodiscard]] std::expected<std::optional<int>, int> finalize() noexcept {
      return std::optional<int>{*m_resource};
    }
  };

  const size_t                     num_jobs = 32;
  std::vector<std::pair<int, int>> needles;
  for (size_t i = 0; i < num_jobs; ++i) {
    needles.emplace_back(static_cast<int>(i), (i % 2 == 0) ? 0 : 10);
  }

  size_t reported_count = 0;
  auto   reporter       = [&]<typename Tag, typename J, typename... Args>(Tag, J&&, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      auto&& payload = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
      REQUIRE(payload >= 0);
      reported_count++;
    }
  };

  auto jobs = needles | std::views::transform([](const auto& needle) { return ResourceJob(needle); });

  vault::amac::executor<16>(jobs, job_context, reporter);

  CHECK(reported_count == num_jobs);
}
