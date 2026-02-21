#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <random>
#include <ranges>
#include <vector>

// --- Test Job Definition ---

struct CountdownState {
  int m_counter;

  explicit CountdownState(int start_count)
    : m_counter(start_count) {}

  [[nodiscard]] int counter() const {
    return m_counter;
  }
};

static constexpr inline struct CountdownContext {
  [[nodiscard]] static constexpr uint64_t fanout() {
    return 1;
  }

  [[nodiscard]] vault::amac::step_result<1> init(CountdownState& state) const {
    if (state.m_counter <= 0) {
      return {nullptr};
    }
    state.m_counter--;
    return {&state};
  }

  [[nodiscard]] vault::amac::step_result<1> step(CountdownState& state) const {
    return init(state);
  }
} countdown_context{};

// Verify Concept Compliance
static_assert(vault::amac::concepts::job_context<const CountdownContext, CountdownState>);

// --- Test Suite ---

TEST_CASE("AMAC Executor: Countdown Integrity", "[amac][executor]") {

  // Test parameters
  const size_t num_jobs        = GENERATE(0, 1, 15, 16, 17, 100, 1000);
  const int    max_start_count = GENERATE(0, 1, 5, 10); // 0 ensures we test immediate completion

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

  auto reporter = [&](CountdownState&& state) {
    reported_count++;
    // CRITICAL CHECK: Job must be finished (counter == 0)
    REQUIRE(state.counter() == 0);
  };

  // 3. Create Jobs View (Lazy Construction)
  // We transform the vector of integers into a range of CountdownState objects.
  auto states = start_counts | std::views::transform([](int count) { return CountdownState(count); });

  // 4. Run Executor (Batch Size 16)
  vault::amac::executor<16>(states, countdown_context, reporter);

  // 5. Verification
  // Condition: Reported is invoked N times
  CHECK(reported_count == num_jobs);
}

TEST_CASE("AMAC Executor: Batch Size Sensitivity", "[amac][batch_size]") {
  // Verify logic holds for degenerate batch sizes (1 = Serial, 2 = Minimal
  // Parallel)

  const size_t     num_jobs = 100;
  std::vector<int> start_counts(num_jobs, 5); // All jobs take 5 steps

  size_t reported_count = 0;
  auto   reporter       = [&](CountdownState&& state) {
    reported_count++;
    REQUIRE(state.counter() == 0);
  };

  auto states = start_counts | std::views::transform([](int count) { return CountdownState(count); });

  SECTION("Batch Size = 1 (Serial Execution)") {
    vault::amac::executor<1>(states, countdown_context, reporter);
    CHECK(reported_count == num_jobs);
  }

  SECTION("Batch Size = 2") {
    vault::amac::executor<2>(states, countdown_context, reporter);
    CHECK(reported_count == num_jobs);
  }
}

TEST_CASE("AMAC Executor: Immediate Completion Edge Cases", "[amac][edge_cases]") {
  // Specifically target the "init returns nullptr" logic

  size_t reported_count = 0;
  auto   reporter       = [&](CountdownState&& state) {
    reported_count++;
    REQUIRE(state.counter() == 0);
  };

  SECTION("All jobs finish immediately") {
    // Needle 0 -> Counter 0 -> init() returns nullptr immediately
    std::vector<int> zeros(50, 0);

    auto states = zeros | std::views::transform([](int count) { return CountdownState(count); });

    vault::amac::executor<16>(states, countdown_context, reporter);
    CHECK(reported_count == 50);
  }

  SECTION("Mixed immediate and long-running") {
    // Interleave 0s and 10s
    std::vector<int> mixed;
    for (int i = 0; i < 100; ++i) {
      mixed.push_back(i % 2 == 0 ? 0 : 10);
    }

    auto states = mixed | std::views::transform([](int count) { return CountdownState(count); });

    vault::amac::executor<16>(states, countdown_context, reporter);
    CHECK(reported_count == 100);
  }
}

TEST_CASE("AMAC Executor: Double Free Regression Test", "[amac][resource][asan]") {
  // This test uses std::unique_ptr to detect double-free bugs.
  // If the executor performs a shallow byte-copy of the job during
  // compaction (std::remove_if) instead of a proper move assignment, the
  // unique_ptr in the source slot will remain valid. When both slots are
  // eventually destroyed, the same pointer will be deleted twice, triggering
  // ASan/UBSan.

  struct ResourceState {
    std::unique_ptr<int> m_resource;
    int                  m_steps_remaining;

    // Needles are pairs: {id, steps}
    using Needle = std::pair<int, int>;

    explicit ResourceState(Needle n)
      : m_resource(std::make_unique<int>(n.first))
      , m_steps_remaining(n.second) {}

    // Disable copy, allow move (standard for unique_ptr wrappers)
    ResourceState(const ResourceState&)            = delete;
    ResourceState& operator=(const ResourceState&) = delete;
    ResourceState(ResourceState&&)                 = default;
    ResourceState& operator=(ResourceState&&)      = default;

    [[nodiscard]] int id() const {
      return *m_resource;
    }
  };

  static constexpr struct ResourceContext {
    [[nodiscard]] static constexpr uint64_t fanout() {
      return 1;
    }

    [[nodiscard]] vault::amac::step_result<1> init(ResourceState& state) const {
      if (state.m_steps_remaining <= 0) {
        return {nullptr};
      }
      state.m_steps_remaining--;
      // Return address of heap data to simulate dependency
      return {state.m_resource.get()};
    }

    [[nodiscard]] vault::amac::step_result<1> step(ResourceState& state) const {
      return init(state);
    }
  } resource_context{};

  // 1. Setup Inputs
  // We need a specific pattern to force compaction (std::remove_if):
  // [Finish, Run, Finish, Run, ...]
  // This creates "holes" at indices 0, 2, 4... requiring jobs at 1, 3, 5... to
  // shift left.
  const size_t                     num_jobs = 32;
  std::vector<std::pair<int, int>> needles;
  needles.reserve(num_jobs);

  for (size_t i = 0; i < num_jobs; ++i) {
    // Even indices finish immediately (0 steps).
    // Odd indices run for 10 steps.
    int steps = (i % 2 == 0) ? 0 : 10;
    needles.emplace_back(static_cast<int>(i), steps);
  }

  size_t reported_count = 0;

  auto reporter = [&](ResourceState&& state) {
    // Touch the resource to ensure it's valid
    volatile int x = state.id();
    (void)x;
    reported_count++;
  };

  // 2. Create Jobs View
  auto states = needles | std::views::transform([](const auto& needle) { return ResourceState(needle); });

  // 3. Run with Batch Size 16
  // This ensures we have enough active slots to trigger the "holes" pattern.
  vault::amac::executor<16>(states, resource_context, reporter);

  // 4. Verify
  CHECK(reported_count == num_jobs);
}
