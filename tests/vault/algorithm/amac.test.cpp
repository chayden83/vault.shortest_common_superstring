#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <random>
#include <ranges>
#include <stdexcept>
#include <vector>

// --- Helper Receiver ---
template <typename J> struct TestReceiver {
  std::function<void(J&&)>                     completion_handler;
  std::function<void(J&&, std::exception_ptr)> failure_handler;

  void on_completion(J&& job)
  {
    if (completion_handler) {
      completion_handler(std::move(job));
    }
  }

  void on_failure(J&& job, std::exception_ptr e)
  {
    if (failure_handler) {
      failure_handler(std::move(job), e);
    }
  }
};

// --- Test Job Definitions ---

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

  TestReceiver<CountdownState> reporter;
  reporter.completion_handler = [&](CountdownState&& job) {
    reported_count++;
    // CRITICAL CHECK: Job must be finished (counter == 0)
    REQUIRE(job.m_counter == 0);
  };
  reporter.failure_handler = [&](CountdownState&&, std::exception_ptr) {
    FAIL("Should not have failed");
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

  size_t                       reported_count = 0;
  TestReceiver<CountdownState> reporter;
  reporter.completion_handler = [&](CountdownState&& job) {
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
  size_t                       reported_count = 0;
  TestReceiver<CountdownState> reporter;
  reporter.completion_handler = [&](CountdownState&& job) {
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

  TestReceiver<ResourceState> reporter;
  reporter.completion_handler = [&](ResourceState&& job) {
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

TEST_CASE("AMAC Executor: Error Channel", "[amac][error]")
{
  // A job state with an ID to verify which one failed
  struct FragileState {
    int  id;
    bool should_throw;
  };

  struct FragileContext {
    static constexpr uint64_t fanout() { return 1; }

    vault::amac::step_result<1> init(FragileState& s)
    {
      if (s.should_throw) {
        throw std::runtime_error("Boom");
      }
      return {nullptr}; // Finish immediately
    }

    vault::amac::step_result<1> step(FragileState&) { return {nullptr}; }
  };

  std::vector<FragileState> inputs = {{1, false},
    {2, true}, // This will explode
    {3, false}};

  size_t success_count = 0;
  size_t fail_count    = 0;

  TestReceiver<FragileState> reporter;
  reporter.completion_handler = [&](FragileState&& s) {
    success_count++;
    CHECK((s.id == 1 || s.id == 3));
  };

  reporter.failure_handler = [&](FragileState&& s, std::exception_ptr e) {
    fail_count++;
    CHECK(s.id == 2);
    try {
      std::rethrow_exception(e);
    } catch (const std::runtime_error& ex) {
      CHECK(std::string(ex.what()) == "Boom");
    }
  };

  FragileContext ctx;
  vault::amac::executor<4>(ctx, inputs, reporter);

  CHECK(success_count == 2);
  CHECK(fail_count == 1);
}

// --- Policy Test Definitions ---

struct ThrowingReporterState {
  int id;
};

struct ThrowingReporterContext {
  static constexpr uint64_t fanout() { return 1; }

  vault::amac::step_result<1> init(ThrowingReporterState&) { return {nullptr}; }

  vault::amac::step_result<1> step(ThrowingReporterState&) { return {nullptr}; }
};

template <typename J> struct EvilReceiver {
  void on_completion(J&&) { throw std::runtime_error("Double Fault"); }

  void on_failure(J&&, std::exception_ptr)
  {
    throw std::runtime_error("Double Fault");
  }
};

TEST_CASE("AMAC Executor: Double Fault Policies", "[amac][policy]")
{
  std::vector<ThrowingReporterState>  inputs = {{1}};
  ThrowingReporterContext             ctx;
  EvilReceiver<ThrowingReporterState> reporter;

  SECTION("Policy: Terminate (Default)")
  {
    // Compile-time check for default type
    STATIC_REQUIRE(std::is_same_v<decltype(vault::amac::executor<1>),
      const vault::amac::executor_fn<1,
        vault::amac::double_fault_policy::terminate>>);
  }

  SECTION("Policy: Suppress")
  {
    REQUIRE_NOTHROW(
      (vault::amac::executor<1, vault::amac::double_fault_policy::suppress>(
        ctx, inputs, reporter)));
  }

  SECTION("Policy: Rethrow")
  {
    REQUIRE_THROWS_AS(
      (vault::amac::executor<1, vault::amac::double_fault_policy::rethrow>(
        ctx, inputs, reporter)),
      std::runtime_error);
  }
}

TEST_CASE("AMAC Executor: RAII Cleanup on Rethrow", "[amac][raii]")
{
  static int destroyed_count = 0;

  struct TrackedState {
    int id;

    ~TrackedState() { destroyed_count++; }

    TrackedState(int i)
        : id(i)
    {}

    TrackedState(TrackedState&&)                 = default;
    TrackedState& operator=(TrackedState&&)      = default;
    TrackedState(const TrackedState&)            = delete;
    TrackedState& operator=(const TrackedState&) = delete;
  };

  struct TriggerContext {
    static constexpr uint64_t fanout() { return 1; }

    // Job 1 fails and triggers double fault. Job 2 is active.
    vault::amac::step_result<1> init(TrackedState& s)
    {
      if (s.id == 1) {
        throw std::runtime_error("Trigger");
      }
      return {&s};
    }

    vault::amac::step_result<1> step(TrackedState&) { return {nullptr}; }
  };

  std::vector<TrackedState> inputs;
  inputs.reserve(2);
  // Order matters:
  // 1. Job 2 (Safe) enters first and occupies a slot.
  // 2. Job 1 (Trigger) enters second, throws, and causes the executor to
  // unwind.
  inputs.emplace_back(2);
  inputs.emplace_back(1);

  // Explicitly transform input vector (lvalues) into rvalues using a view
  auto rvalue_inputs = inputs
    | std::views::transform(
      [](TrackedState& s) -> TrackedState&& { return std::move(s); });

  // EvilReceiver throws on failure
  EvilReceiver<TrackedState> reporter;
  TriggerContext             ctx;

  // Reset count to ignore vector setup noise
  destroyed_count = 0;

  REQUIRE_THROWS(
    (vault::amac::executor<4, vault::amac::double_fault_policy::rethrow>(
      ctx, rvalue_inputs, reporter)));

  // We expect EXACTLY 1 destruction here:
  // Job 2 was active in the slot and should be destroyed by the scope_guard.
  // Job 1 threw before entering a slot, so the guard doesn't see it.
  // The vector 'inputs' is still alive, so its destructors haven't run yet.
  CHECK(destroyed_count == 1);
}
