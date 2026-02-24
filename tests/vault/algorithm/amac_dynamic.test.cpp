// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <optional>
#include <vector>

#include <vault/algorithm/amac_dynamic.hpp>

namespace vault::amac::testing {

  // A mock job that tracks its own lifecycle to strictly verify
  // memory safety and detect double-frees or leaks.
  struct dynamic_job {
    int  id;
    int  depth;
    int  steps_remaining;
    bool should_fail;
    bool should_terminate;

    static inline int alive_count = 0;

    dynamic_job(int i, int d, int s = 1, bool f = false, bool t = false)
      : id(i)
      , depth(d)
      , steps_remaining(s)
      , should_fail(f)
      , should_terminate(t) {
      ++alive_count;
    }

    dynamic_job(dynamic_job const& o)
      : id(o.id)
      , depth(o.depth)
      , steps_remaining(o.steps_remaining)
      , should_fail(o.should_fail)
      , should_terminate(o.should_terminate) {
      ++alive_count;
    }

    dynamic_job(dynamic_job&& o) noexcept
      : id(o.id)
      , depth(o.depth)
      , steps_remaining(o.steps_remaining)
      , should_fail(o.should_fail)
      , should_terminate(o.should_terminate) {
      ++alive_count;
    }

    ~dynamic_job() {
      --alive_count;
    }

    dynamic_job& operator=(dynamic_job const&) = default;
    dynamic_job& operator=(dynamic_job&&)      = default;
  };

  // A mock context that simulates traversing a DAG by spawning children.
  struct dynamic_mock_context {
    int max_depth;
    int fanout_per_node;

    [[nodiscard]] static constexpr std::size_t fanout() noexcept {
      return 1;
    }

    template <typename Sink>
    auto init(dynamic_job& j, Sink& /* sink */) noexcept -> std::expected<vault::amac::step_result<1>, int> {
      if (j.steps_remaining == 0) {
        // Synchronous completion: array of nullptrs evaluates to false
        return vault::amac::step_result<1>{nullptr};
      }
      return vault::amac::step_result<1>{&j};
    }

    template <typename Sink>
    auto step(dynamic_job& j, Sink& /* sink */) noexcept -> std::expected<vault::amac::step_result<1>, int> {
      if (j.should_fail) {
        return std::unexpected(-1);
      }
      if (--j.steps_remaining > 0) {
        return vault::amac::step_result<1>{&j};
      }
      return vault::amac::step_result<1>{nullptr};
    }

    template <typename Sink>
    auto finalize(dynamic_job& j, Sink& sink) noexcept -> std::expected<std::optional<int>, int> {
      if (j.should_fail) {
        return std::unexpected(-1);
      }
      if (j.should_terminate) {
        return std::nullopt;
      }

      // Spawns children directly into the Sink if we haven't hit the max depth.
      if (j.depth < max_depth) {
        for (int i = 0; i < fanout_per_node; ++i) {
          sink(dynamic_job{j.id * 10 + i, j.depth + 1, 1});
        }
      }
      return j.id; // Payload is simply the job's ID
    }
  };

  struct mock_reporter {
    int              completed_count  = 0;
    int              terminated_count = 0;
    int              failed_count     = 0;
    std::vector<int> completed_payloads;

    void operator()(vault::amac::completed_tag, dynamic_job&&, int&& payload) {
      ++completed_count;
      completed_payloads.push_back(payload);
    }

    void operator()(vault::amac::terminated_tag, dynamic_job&&) {
      ++terminated_count;
    }

    void operator()(vault::amac::failed_tag, dynamic_job&&, int&& /* error */) {
      ++failed_count;
    }
  };

} // namespace vault::amac::testing

TEST_CASE("Dynamic AMAC Executor Handles Empty Source", "[amac][dynamic]") {
  using namespace vault::amac::testing;
  dynamic_job::alive_count = 0;

  auto queue  = std::vector<dynamic_job>{};
  auto source = [&]() -> std::optional<dynamic_job> { return std::nullopt; };
  auto sink   = [&](dynamic_job&& j) { queue.push_back(std::move(j)); };

  auto ctx      = dynamic_mock_context{.max_depth = 0, .fanout_per_node = 0};
  auto reporter = mock_reporter{};

  vault::amac::dynamic_executor<4>(ctx, reporter, source, sink);

  REQUIRE(reporter.completed_count == 0);
  REQUIRE(queue.empty());
  REQUIRE(dynamic_job::alive_count == 0);
}

TEST_CASE("Dynamic AMAC Executor Handles Synchronous Completion", "[amac][dynamic]") {
  using namespace vault::amac::testing;
  dynamic_job::alive_count = 0;

  auto queue = std::vector<dynamic_job>{};
  // steps_remaining = 0 triggers synchronous completion in init()
  queue.emplace_back(1, 0, 0);

  auto source = [&]() -> std::optional<dynamic_job> {
    if (queue.empty()) {
      return std::nullopt;
    }
    auto j = std::move(queue.back());
    queue.pop_back();
    return j;
  };

  auto sink     = [&](dynamic_job&& j) { queue.push_back(std::move(j)); };
  auto ctx      = dynamic_mock_context{.max_depth = 0, .fanout_per_node = 0};
  auto reporter = mock_reporter{};

  vault::amac::dynamic_executor<4>(ctx, reporter, source, sink);

  REQUIRE(reporter.completed_count == 1);
  REQUIRE(reporter.completed_payloads[0] == 1);
  REQUIRE(queue.empty());
  REQUIRE(dynamic_job::alive_count == 0);
}

TEST_CASE("Dynamic AMAC Executor Dynamically Spawns Children (DAG Traversal)", "[amac][dynamic]") {
  using namespace vault::amac::testing;
  dynamic_job::alive_count = 0;

  auto queue = std::vector<dynamic_job>{};
  queue.emplace_back(1, 0, 1); // Root node, ID=1, Depth=0, Steps=1

  auto source = [&]() -> std::optional<dynamic_job> {
    if (queue.empty()) {
      return std::nullopt;
    }
    auto j = std::move(queue.back());
    queue.pop_back();
    return j;
  };

  auto sink = [&](dynamic_job&& j) { queue.push_back(std::move(j)); };

  // Depth 0 -> Depth 1 -> Depth 2. Fanout 2.
  // Total nodes = 1 (root) + 2 (depth 1) + 4 (depth 2) = 7 nodes.
  auto ctx      = dynamic_mock_context{.max_depth = 2, .fanout_per_node = 2};
  auto reporter = mock_reporter{};

  vault::amac::dynamic_executor<4>(ctx, reporter, source, sink);

  REQUIRE(reporter.completed_count == 7);
  REQUIRE(reporter.failed_count == 0);
  REQUIRE(reporter.terminated_count == 0);
  REQUIRE(queue.empty());

  // Confirms the absolute absence of memory leaks during dynamic push/pops
  REQUIRE(dynamic_job::alive_count == 0);
}

TEST_CASE("Dynamic AMAC Executor Handles Failures and Terminations", "[amac][dynamic]") {
  using namespace vault::amac::testing;
  dynamic_job::alive_count = 0;

  auto queue = std::vector<dynamic_job>{};
  // Job 1: Completes normally
  queue.emplace_back(1, 0, 1, false, false);
  // Job 2: Fails
  queue.emplace_back(2, 0, 1, true, false);
  // Job 3: Terminates
  queue.emplace_back(3, 0, 1, false, true);

  auto source = [&]() -> std::optional<dynamic_job> {
    if (queue.empty()) {
      return std::nullopt;
    }
    auto j = std::move(queue.back());
    queue.pop_back();
    return j;
  };

  auto sink     = [&](dynamic_job&& j) { queue.push_back(std::move(j)); };
  auto ctx      = dynamic_mock_context{.max_depth = 0, .fanout_per_node = 0};
  auto reporter = mock_reporter{};

  vault::amac::dynamic_executor<4>(ctx, reporter, source, sink);

  REQUIRE(reporter.completed_count == 1);
  REQUIRE(reporter.failed_count == 1);
  REQUIRE(reporter.terminated_count == 1);
  REQUIRE(queue.empty());
  REQUIRE(dynamic_job::alive_count == 0);
}

TEST_CASE("Dynamic AMAC Executor Interleaves Multi-Step Jobs", "[amac][dynamic]") {
  using namespace vault::amac::testing;
  dynamic_job::alive_count = 0;

  auto queue = std::vector<dynamic_job>{};
  // Push 3 jobs that take different numbers of async steps to complete
  queue.emplace_back(1, 0, 5); // Takes 5 steps
  queue.emplace_back(2, 0, 2); // Takes 2 steps
  queue.emplace_back(3, 0, 8); // Takes 8 steps

  auto source = [&]() -> std::optional<dynamic_job> {
    if (queue.empty()) {
      return std::nullopt;
    }
    auto j = std::move(queue.back());
    queue.pop_back();
    return j;
  };

  auto sink     = [&](dynamic_job&& j) { queue.push_back(std::move(j)); };
  auto ctx      = dynamic_mock_context{.max_depth = 0, .fanout_per_node = 0};
  auto reporter = mock_reporter{};

  // Fanout of 4 ensures all 3 jobs run concurrently
  vault::amac::dynamic_executor<4>(ctx, reporter, source, sink);

  REQUIRE(reporter.completed_count == 3);
  REQUIRE(reporter.failed_count == 0);
  REQUIRE(queue.empty());

  // If the executor accidentally compacted a running job, alive_count would be wrong
  // or ASan would flag a use-after-free.
  REQUIRE(dynamic_job::alive_count == 0);
}

TEST_CASE("Dynamic AMAC Executor Handles 'Last Man Standing' Self-Compaction", "[amac][dynamic]") {
  using namespace vault::amac::testing;
  dynamic_job::alive_count = 0;

  auto queue = std::vector<dynamic_job>{};
  // A single job in a pipeline of fanout 16.
  queue.emplace_back(1, 0, 1);

  auto source = [&]() -> std::optional<dynamic_job> {
    if (queue.empty()) {
      return std::nullopt;
    }
    auto j = std::move(queue.back());
    queue.pop_back();
    return j;
  };

  auto sink     = [&](dynamic_job&& j) { queue.push_back(std::move(j)); };
  auto ctx      = dynamic_mock_context{.max_depth = 0, .fanout_per_node = 0};
  auto reporter = mock_reporter{};

  // When this single job finishes, jobs_active_end will be 1.
  // The executor will call: it->compact_from(*std::prev(jobs_active_end))
  // This means it compacts slot 0 from slot 0.
  vault::amac::dynamic_executor<16>(ctx, reporter, source, sink);

  REQUIRE(reporter.completed_count == 1);
  REQUIRE(dynamic_job::alive_count == 0); // Fails immediately if double-free occurs
}

TEST_CASE("Dynamic AMAC Executor Triggers Phase 2b Top-Up on Massive Fanout", "[amac][dynamic]") {
  using namespace vault::amac::testing;
  dynamic_job::alive_count = 0;

  auto queue = std::vector<dynamic_job>{};
  // Root node completes in 1 step, but will spawn 20 children.
  queue.emplace_back(1, 0, 1);

  auto source = [&]() -> std::optional<dynamic_job> {
    if (queue.empty()) {
      return std::nullopt;
    }
    auto j = std::move(queue.back());
    queue.pop_back();
    return j;
  };

  auto sink = [&](dynamic_job&& j) { queue.push_back(std::move(j)); };

  // Max depth 1 limits it to just the root's children.
  // Fanout 20 > AMAC Fanout 16.
  auto ctx      = dynamic_mock_context{.max_depth = 1, .fanout_per_node = 20};
  auto reporter = mock_reporter{};

  // The executor has 16 slots. The root finishes, opening 1 slot.
  // The sink now has 20 items. Refill consumes 1.
  // Phase 2b must trigger to pull 15 more items into the active window.
  vault::amac::dynamic_executor<16>(ctx, reporter, source, sink);

  REQUIRE(reporter.completed_count == 21); // 1 root + 20 children
  REQUIRE(queue.empty());
  REQUIRE(dynamic_job::alive_count == 0);
}
