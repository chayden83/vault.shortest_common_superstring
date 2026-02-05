#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <algorithm>
#include <functional>
#include <random>
#include <ranges>
#include <stdexcept>
#include <variant>
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

// --- Global Test State Definitions ---

struct CountdownState {
  int m_counter;
};

struct CountdownContext {
  [[nodiscard]] static constexpr uint64_t fanout() { return 1uz; }

  template <typename Emit>
  [[nodiscard]] vault::amac::step_result<1> init(
    CountdownState& state, Emit&&) const
  {
    if (state.m_counter <= 0) {
      return {nullptr};
    }
    state.m_counter--;
    return {&state};
  }

  template <typename Emit>
  [[nodiscard]] vault::amac::step_result<1> step(
    CountdownState& state, Emit&& e) const
  {
    return init(state, e);
  }
};

static_assert(vault::amac::concepts::context<CountdownContext, CountdownState>);

struct ForkState {
  int id;
  int count;
  int depth;
};

struct ForkContext {
  static constexpr uint64_t fanout() { return 2; }

  template <typename Emit>
  vault::amac::step_result<2> init(ForkState& s, Emit&&)
  {
    if (s.count <= 0) {
      return {nullptr, nullptr};
    }
    s.count--;
    return {&s, &s};
  }

  template <typename Emit>
  vault::amac::step_result<2> step(ForkState& s, Emit&& emit)
  {
    if (s.count == 1 && s.depth < 1) {
      emit(ForkState{s.id * 10 + 1, 2, s.depth + 1});
      emit(ForkState{s.id * 10 + 2, 2, s.depth + 1});
    }

    if (s.count <= 0) {
      return {nullptr, nullptr};
    }
    s.count--;
    return {&s, &s};
  }
};

struct ResourceState {
  std::unique_ptr<int> m_resource;
  int                  m_steps_remaining;

  ResourceState(int id, int steps)
      : m_resource(std::make_unique<int>(id))
      , m_steps_remaining(steps)
  {}

  ResourceState(ResourceState&&)                 = default;
  ResourceState& operator=(ResourceState&&)      = default;
  ResourceState(const ResourceState&)            = delete;
  ResourceState& operator=(const ResourceState&) = delete;
};

struct ResourceContext {
  [[nodiscard]] static constexpr uint64_t fanout() { return 1uz; }

  template <typename Emit>
  [[nodiscard]] vault::amac::step_result<1> init(
    ResourceState& state, Emit&&) const
  {
    if (state.m_steps_remaining <= 0) {
      return {nullptr};
    }
    state.m_steps_remaining--;
    return {state.m_resource.get()};
  }

  template <typename Emit>
  [[nodiscard]] vault::amac::step_result<1> step(
    ResourceState& state, Emit&& e) const
  {
    return init(state, e);
  }
};

struct FragileState {
  int  id;
  bool should_throw;
};

struct FragileContext {
  static constexpr uint64_t fanout() { return 1; }

  template <typename Emit>
  vault::amac::step_result<1> init(FragileState& s, Emit&&)
  {
    if (s.should_throw) {
      throw std::runtime_error("Boom");
    }
    return {nullptr};
  }

  template <typename Emit>
  vault::amac::step_result<1> step(FragileState&, Emit&&)
  {
    return {nullptr};
  }
};

struct ThrowingReporterState {
  int id;
};

struct ThrowingReporterContext {
  static constexpr uint64_t fanout() { return 1; }

  template <typename Emit>
  vault::amac::step_result<1> init(ThrowingReporterState&, Emit&&)
  {
    return {nullptr};
  }

  template <typename Emit>
  vault::amac::step_result<1> step(ThrowingReporterState&, Emit&&)
  {
    return {nullptr};
  }
};

template <typename J> struct EvilReceiver {
  void on_completion(J&&) { throw std::runtime_error("Double Fault"); }

  void on_failure(J&&, std::exception_ptr)
  {
    throw std::runtime_error("Double Fault");
  }
};

static int g_destroyed_count = 0;

struct TrackedState {
  int id;

  ~TrackedState() { g_destroyed_count++; }

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

  template <typename Emit>
  vault::amac::step_result<1> init(TrackedState& s, Emit&&)
  {
    if (s.id == 1) {
      throw std::runtime_error("Trigger");
    }
    return {&s};
  }

  template <typename Emit>
  vault::amac::step_result<1> step(TrackedState&, Emit&&)
  {
    return {nullptr};
  }
};

// --- Test Suite ---

TEST_CASE("AMAC Executor: Countdown Integrity", "[amac][executor]")
{
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

  TestReceiver<CountdownState> reporter;
  reporter.completion_handler = [&](CountdownState&& job) {
    reported_count++;
    REQUIRE(job.m_counter == 0);
  };
  reporter.failure_handler = [&](CountdownState&&, std::exception_ptr) {
    FAIL("Should not have failed");
  };

  auto jobs = start_counts
    | std::views::transform([](int count) { return CountdownState{count}; });

  CountdownContext ctx;
  vault::amac::executor<16>(ctx, jobs, reporter);

  CHECK(reported_count == num_jobs);
}

TEST_CASE("AMAC Executor: Dynamic Forking", "[amac][fork]")
{
  std::vector<ForkState> inputs = {{1, 3, 0}};
  std::vector<int>       completed_ids;

  TestReceiver<ForkState> reporter;
  reporter.completion_handler = [&](ForkState&& s) {
    completed_ids.push_back(s.id);
  };

  ForkContext ctx;
  vault::amac::executor<16>(ctx, inputs, reporter);

  CHECK(completed_ids.size() == 3);

  std::ranges::sort(completed_ids);
  CHECK(completed_ids == std::vector<int>{1, 11, 12});
}

TEST_CASE("AMAC Executor: Composition Chaining", "[amac][chain]")
{
  auto transition = [](CountdownState&& c) -> std::optional<ForkState> {
    if (c.m_counter == 0) {
      return ForkState{1, 2, 0};
    }
    return std::nullopt;
  };

  using JobA = CountdownState;
  using JobB = ForkState;

  auto chain = vault::amac::chain<JobA, JobB>(
    CountdownContext{}, ForkContext{}, transition);

  using VariantJob = std::variant<JobA, JobB>;
  std::vector<VariantJob> inputs;
  inputs.emplace_back(JobA{1});

  std::vector<int>         completed_ids;
  TestReceiver<VariantJob> reporter;
  reporter.completion_handler = [&](VariantJob&& v) {
    if (std::holds_alternative<JobB>(v)) {
      completed_ids.push_back(std::get<JobB>(v).id);
    } else {
      FAIL("JobA should have transitioned");
    }
  };

  vault::amac::executor<16>(chain, inputs, reporter);

  CHECK(completed_ids.size() == 3);
  std::ranges::sort(completed_ids);
  CHECK(completed_ids == std::vector<int>{1, 11, 12});

  static_assert(decltype(chain)::fanout() == 2);
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
  "AMAC Executor: Double Free Regression Test", "[amac][resource][asan]")
{
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
  std::vector<FragileState> inputs = {{1, false}, {2, true}, {3, false}};

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

TEST_CASE("AMAC Executor: Double Fault Policies", "[amac][policy]")
{
  std::vector<ThrowingReporterState>  inputs = {{1}};
  ThrowingReporterContext             ctx;
  EvilReceiver<ThrowingReporterState> reporter;

  SECTION("Policy: Terminate (Default)")
  {
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
  std::vector<TrackedState> inputs;
  inputs.reserve(2);
  inputs.emplace_back(2);
  inputs.emplace_back(1);

  auto rvalue_inputs = inputs
    | std::views::transform(
      [](TrackedState& s) -> TrackedState&& { return std::move(s); });

  EvilReceiver<TrackedState> reporter;
  TriggerContext             ctx;

  g_destroyed_count = 0;

  REQUIRE_THROWS(
    (vault::amac::executor<4, vault::amac::double_fault_policy::rethrow>(
      ctx, rvalue_inputs, reporter)));

  CHECK(g_destroyed_count == 1);
}
