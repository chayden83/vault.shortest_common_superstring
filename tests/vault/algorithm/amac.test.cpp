#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <expected>
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
} job_context{};

// --- Test Jobs ---

// A synthetic job that counts down from a specific number.
// Used to verify that AMAC runs every job to completion.
class CountdownJob {
  int m_counter;

public:
  // Needles are simple integers representing the starting count
  using Needle = int;

  explicit CountdownJob(int start_count)
    : m_counter(start_count) {}

  // AMAC Requirements

  // Returns a tuple-like result of 1 pointer wrapped in std::expected.
  // If counter is 0, returns {nullptr} (Finished).
  // Otherwise, decrements and returns {this} (Active).
  [[nodiscard]] std::expected<vault::amac::step_result<1>, int> init() noexcept {
    if (m_counter <= 0) {
      return vault::amac::step_result<1>{nullptr};
    }
    m_counter--;
    return vault::amac::step_result<1>{this};
  }

  [[nodiscard]] std::expected<vault::amac::step_result<1>, int> step() noexcept {
    return init(); // Step logic is identical to init for this test
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
      return std::unexpected(404); // Return an arbitrary error code
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

  // The new tagged dispatch reporter
  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&...) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      reported_count++;
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
    // Evens complete successfully, Odds fail when counter hits 2
    int start_count = 5;
    int fail_at     = (i % 2 == 0) ? -1 : 2;
    configs.emplace_back(start_count, fail_at);
  }

  size_t completed_count = 0;
  size_t failed_count    = 0;

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&... args) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      completed_count++;
      REQUIRE(job.counter() == 0);
    } else if constexpr (std::is_same_v<Tag, vault::amac::failed_tag>) {
      failed_count++;
      REQUIRE(job.counter() == 2);
      // Verify we received the expected error code
      auto err = std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...));
      REQUIRE(err == 404);
    } else {
      FAIL("Unexpected tag received.");
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
  auto   reporter       = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&...) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      reported_count++;
      REQUIRE(job.counter() == 0);
    }
  };

  auto jobs = start_counts | std::views::transform([](int count) { return CountdownJob(count); });

  SECTION("Batch Size = 1 (Serial Execution)") {
    vault::amac::executor<1>(jobs, job_context, reporter);
    CHECK(reported_count == num_jobs);
  }

  SECTION("Batch Size = 2") {
    vault::amac::executor<2>(jobs, job_context, reporter);
    CHECK(reported_count == num_jobs);
  }
}

TEST_CASE("AMAC Executor: Immediate Completion Edge Cases", "[amac][edge_cases]") {
  size_t reported_count = 0;
  auto   reporter       = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&...) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      reported_count++;
      REQUIRE(job.counter() == 0);
    }
  };

  SECTION("All jobs finish immediately") {
    std::vector<int> zeros(50, 0);
    auto             jobs = zeros | std::views::transform([](int count) { return CountdownJob(count); });
    vault::amac::executor<16>(jobs, job_context, reporter);
    CHECK(reported_count == 50);
  }

  SECTION("Mixed immediate and long-running") {
    std::vector<int> mixed;
    for (int i = 0; i < 100; ++i) {
      mixed.push_back(i % 2 == 0 ? 0 : 10);
    }
    auto jobs = mixed | std::views::transform([](int count) { return CountdownJob(count); });
    vault::amac::executor<16>(jobs, job_context, reporter);
    CHECK(reported_count == 100);
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

    [[nodiscard]] int id() const {
      return *m_resource;
    }
  };

  const size_t                     num_jobs = 32;
  std::vector<std::pair<int, int>> needles;
  needles.reserve(num_jobs);

  for (size_t i = 0; i < num_jobs; ++i) {
    int steps = (i % 2 == 0) ? 0 : 10;
    needles.emplace_back(static_cast<int>(i), steps);
  }

  size_t reported_count = 0;

  auto reporter = [&]<typename Tag, typename J, typename... Args>(Tag, J&& job, Args&&...) {
    if constexpr (std::is_same_v<Tag, vault::amac::completed_tag>) {
      volatile int x = job.id();
      (void)x;
      reported_count++;
    }
  };

  auto jobs = needles | std::views::transform([](const auto& needle) { return ResourceJob(needle); });

  vault::amac::executor<16>(jobs, job_context, reporter);

  CHECK(reported_count == num_jobs);
}
