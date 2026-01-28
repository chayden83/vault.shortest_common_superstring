#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <atomic>
#include <random>
#include <vector>

// --- Test Job Definition ---

// A synthetic job that counts down from a specific number.
// Used to verify that AMAC runs every job to completion.
class CountdownJob {
  int m_counter;

public:
  // Needles are simple integers representing the starting count
  using Needle = int;

  explicit CountdownJob(int start_count)
      : m_counter(start_count)
  {}

  // AMAC Requirements

  // Returns a tuple-like result of 1 pointer.
  // If counter is 0, returns {nullptr} (Finished).
  // Otherwise, decrements and returns {this} (Active).
  [[nodiscard]] vault::amac::job_step_result<1> init()
  {
    if (m_counter <= 0) {
      return {nullptr};
    }
    m_counter--;
    // We return 'this' as the pointer to "prefetch", which is valid logic
    // (prefetching the job state itself).
    return {this};
  }

  [[nodiscard]] vault::amac::job_step_result<1> step()
  {
    return init(); // Step logic is identical to init for this test
  }

  [[nodiscard]] int counter() const { return m_counter; }
};

// Verify Concept Compliance
static_assert(vault::amac::concepts::job<CountdownJob>);

// --- Test Suite ---

TEST_CASE("AMAC Coordinator: Countdown Integrity", "[amac][coordinator]")
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

  // 2. Setup Haystack (Dummy)
  std::vector<int> dummy_haystack;

  // 3. Setup Reporting
  size_t reported_count = 0;

  auto reporter = [&](CountdownJob&& job) {
    reported_count++;
    // CRITICAL CHECK: Job must be finished (counter == 0)
    REQUIRE(job.counter() == 0);
  };

  // 4. Setup Factory
  auto factory = [](const std::vector<int>&, auto needle_it) {
    return CountdownJob(*needle_it);
  };

  // 5. Run Coordinator (Batch Size 16)
  // We use a dummy haystack since our job is self-contained
  vault::amac::coordinator<16>(dummy_haystack, start_counts, factory, reporter);

  // 6. Verification
  // Condition: Reported is invoked N times
  CHECK(reported_count == num_jobs);
}

TEST_CASE("AMAC Coordinator: Batch Size Sensitivity", "[amac][batch_size]")
{
  // Verify logic holds for degenerate batch sizes (1 = Serial, 2 = Minimal
  // Parallel)

  const size_t     num_jobs = 100;
  std::vector<int> start_counts(num_jobs, 5); // All jobs take 5 steps
  std::vector<int> dummy_haystack;

  size_t reported_count = 0;
  auto   reporter       = [&](CountdownJob&& job) {
    reported_count++;
    REQUIRE(job.counter() == 0);
  };

  auto factory = [](const std::vector<int>&, auto needle_it) {
    return CountdownJob(*needle_it);
  };

  SECTION("Batch Size = 1 (Serial Execution)")
  {
    vault::amac::coordinator<1>(
      dummy_haystack, start_counts, factory, reporter);
    CHECK(reported_count == num_jobs);
  }

  SECTION("Batch Size = 2")
  {
    vault::amac::coordinator<2>(
      dummy_haystack, start_counts, factory, reporter);
    CHECK(reported_count == num_jobs);
  }
}

TEST_CASE(
  "AMAC Coordinator: Immediate Completion Edge Cases", "[amac][edge_cases]")
{
  // Specifically target the "init returns nullptr" logic

  std::vector<int> dummy_haystack;
  auto             factory = [](const std::vector<int>&, auto needle_it) {
    return CountdownJob(*needle_it);
  };

  size_t reported_count = 0;
  auto   reporter       = [&](CountdownJob&& job) {
    reported_count++;
    REQUIRE(job.counter() == 0);
  };

  SECTION("All jobs finish immediately")
  {
    // Needle 0 -> Counter 0 -> init() returns nullptr immediately
    std::vector<int> zeros(50, 0);
    vault::amac::coordinator<16>(dummy_haystack, zeros, factory, reporter);
    CHECK(reported_count == 50);
  }

  SECTION("Mixed immediate and long-running")
  {
    // Interleave 0s and 10s
    std::vector<int> mixed;
    for (int i = 0; i < 100; ++i) {
      mixed.push_back(i % 2 == 0 ? 0 : 10);
    }

    vault::amac::coordinator<16>(dummy_haystack, mixed, factory, reporter);
    CHECK(reported_count == 100);
  }
}
