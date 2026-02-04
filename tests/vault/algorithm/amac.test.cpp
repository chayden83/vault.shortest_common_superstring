#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <random>
#include <ranges>
#include <vector>

// --- Test Job Definition ---

// Pure State
struct CountdownState {
  int m_counter;
};

// Behavior / Context
struct CountdownContext {
  [[nodiscard]] static constexpr uint64_t fanout() { return 1uz; }

  // Returns a tuple-like result of 1 pointer.
  // If counter is 0, returns {nullptr} (Finished).
  // Otherwise, decrements and returns {this} (Active).
  [[nodiscard]] vault::amac::step_result<1> init(CountdownState& state) const
  {
    if (state.m_counter <= 0) {
      return {nullptr};
    }
    state.m_counter--;
    return {&state};
  }

  [[nodiscard]] vault::amac::step_result<1> step(CountdownState& state) const
  {
    return init(state);
  }
};

// Verify Concept Compliance
static_assert(vault::amac::concepts::context<CountdownContext, CountdownState>);

// --- Test Suite ---

TEST_CASE("AMAC Executor: Countdown Integrity", "[amac][executor]")
{
  // Test parameters
  const size_t num_jobs = GENERATE(0, 1, 15, 16, 17, 100, 1000);
  const int    max_start_count =
    GENERATE(0, 1, 5, 10); // 0 ensures we test immediate completion

  // 1. Setup Input (Needles)
  std::vector<int> start_counts;
  start_counts.reserve(num_jobs);

  std::mt19937                       rng(42);
  std::uniform_int_distribution<int> dist(0, max_start_count);

  for (size_t i = 0; i < num_jobs; ++i) {
    start_counts.push_back(dist(rng));
  }

  // 2. Setup Reporting
  size_t reported_count = 0;

  auto reporter = [&](CountdownState&& job) {
    reported_count++;
    // CRITICAL CHECK: Job must be finished (counter == 0)
    REQUIRE(job.m_counter == 0);
  };

  // 3. Create Jobs View
  auto jobs = start_counts
    | std::views::transform([](int count) { return CountdownState{count}; });

  // 4. Run Executor
  CountdownContext ctx;
  vault::amac::executor<16>(ctx, jobs, reporter);

  // 5. Verification
  CHECK(reported_count == num_jobs);
}

TEST_CASE("AMAC Executor: Batch Size Sensitivity", "[amac][batch_size]")
{
  const size_t     num_jobs = 100;
  std::vector<int> start_counts(num_jobs, 5);

  size_t reported_count = 0;
  auto   reporter       = [&](CountdownState&& job) {
    reported_count++;
    REQUIRE(job.m_counter == 0);
  };

  auto jobs = start_counts
    | std::views::transform([](int count) { return CountdownState{count}; });

  CountdownContext ctx;

  SECTION("Batch Size = 1 (Serial Execution)")
  {
    vault::amac::executor<1>(ctx, jobs, reporter);
    CHECK(reported_count == num_jobs);
  }

  SECTION("Batch Size = 2")
  {
    vault::amac::executor<2>(ctx, jobs, reporter);
    CHECK(reported_count == num_jobs);
  }
}

TEST_CASE(
  "AMAC Executor: Immediate Completion Edge Cases", "[amac][edge_cases]")
{
  size_t reported_count = 0;
  auto   reporter       = [&](CountdownState&& job) {
    reported_count++;
    REQUIRE(job.m_counter == 0);
  };

  CountdownContext ctx;

  SECTION("All jobs finish immediately")
  {
    std::vector<int> zeros(50, 0);
    auto             jobs = zeros
      | std::views::transform([](int count) { return CountdownState{count}; });

    vault::amac::executor<16>(ctx, jobs, reporter);
    CHECK(reported_count == 50);
  }

  SECTION("Mixed immediate and long-running")
  {
    std::vector<int> mixed;
    for (int i = 0; i < 100; ++i) {
      mixed.push_back(i % 2 == 0 ? 0 : 10);
    }
    auto jobs = mixed
      | std::views::transform([](int count) { return CountdownState{count}; });

    vault::amac::executor<16>(ctx, jobs, reporter);
    CHECK(reported_count == 100);
  }
}

TEST_CASE(
  "AMAC Executor: Double Free Regression Test", "[amac][resource][asan]")
{
  // Pure State holding a move-only resource
  struct ResourceState {
    std::unique_ptr<int> m_resource;
    int                  m_steps_remaining;

    // Explicitly movable, non-copyable
    ResourceState(int id, int steps)
        : m_resource(std::make_unique<int>(id))
        , m_steps_remaining(steps)
    {}

    ResourceState(ResourceState&&)                 = default;
    ResourceState& operator=(ResourceState&&)      = default;
    ResourceState(const ResourceState&)            = delete;
    ResourceState& operator=(const ResourceState&) = delete;
  };

  // Behavior
  struct ResourceContext {
    [[nodiscard]] static constexpr uint64_t fanout() { return 1uz; }

    [[nodiscard]] vault::amac::step_result<1> init(ResourceState& state) const
    {
      if (state.m_steps_remaining <= 0) {
        return {nullptr};
      }
      state.m_steps_remaining--;
      return {state.m_resource.get()};
    }

    [[nodiscard]] vault::amac::step_result<1> step(ResourceState& state) const
    {
      return init(state);
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

  auto reporter = [&](ResourceState&& job) {
    volatile int x = *job.m_resource;
    (void)x;
    reported_count++;
  };

  auto jobs = needles | std::views::transform([](const auto& needle) {
    return ResourceState(needle.first, needle.second);
  });

  ResourceContext ctx;
  vault::amac::executor<16>(ctx, jobs, reporter);

  CHECK(reported_count == num_jobs);
}
