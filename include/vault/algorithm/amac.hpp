// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <tuple>
#include <utility>

/**
 * @defgroup vault_amac Asynchronous Memory Access Coordinator (AMAC)
 *
 * @brief A software-pipelining engine for hiding memory latency.
 *
 * AMAC is a software-pipelining technique designed to hide memory
 * latency by interleaving the execution of multiple independent
 * "jobs." It allows a single CPU core to exploit Memory-Level
 * Parallelism (MLP) by issuing prefetches for multiple jobs before
 * needing the data for any single job.
 *
 * **Memory Pressure and Prefetch Volume**
 *
 * The coordinator manages up to $N$ active jobs
 * simultaneously. During each coordination cycle, the total number of
 * outstanding prefetches is defined as $N \times K$, where $K$ is the
 * number of pointers returned by the Job's `init()` or `step()`
 * method.
 *
 * Users should be aware that high values of $N \times K$ can saturate
 * the CPU's Line Fill Buffers (LFBs). Most modern x86_64 CPUs can
 * track between 10 and 12 outstanding cache line fills. Exceeding
 * this limit may cause the CPU to stall or ignore speculative
 * prefetch requests.
 *
 * **Heuristics for Choosing Batch Size (N)**
 *
 * Selecting the appropriate $N$ is a balance between hiding latency
 * and minimizing overhead. Consider the following factors:
 *
 * 1. **Data Locality**: If the "haystack" fits in the L2 or L3 cache,
 * $N$ should be small (e.g., 2–4). The overhead of the coordinator
 * loop outweighs the benefit of hiding the relatively low latency of
 * a cache hit.
 *
 * 2. **DRAM Access**: For datasets significantly larger than the L3
 * cache, $N$ should be larger (e.g., 8–16) to hide the ~200+ cycle
 * latency of main memory.
 *
 * 3. **Search Type (K)**: For binary searches ($K=1$), $N$ can be
 * higher.  For K-ary searches or multi-probe structures ($K > 1$),
 * $N$ should be reduced to keep the total $N \times K$ product within
 * the limits of the hardware's Fill Buffers.
 *
 * 4. **Job Complexity**: If the `step()` logic involves heavy
 * computation (e.g., complex string comparisons or SIMD), a smaller
 * $N$ is preferred to avoid "compute-stalling" the pipeline while
 * memory is already available.
 */

namespace vault::amac::concepts {
  /**
   * @brief Validates the return type of job state transitions.
   * @ingroup vault_amac
   *
   * A result must be explicitly convertible to bool (true = active,
   * false = done) and satisfy the tuple protocol where every element
   * is a `void const*`.
   */
  template <typename T>
  concept job_step_result = std::constructible_from<bool, T> &&
    []<std::size_t... Is>(std::index_sequence<Is...>) {
      return (std::same_as<void const*, std::tuple_element_t<Is, T>> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});

  /**
   * @brief Defines the interface for an AMAC-compatible job.
   * @ingroup vault_amac
   *
   * A Job is a state machine that transitions via init() and step().
   *
   * @note **State Monotonicity Requirement**: The predicate result of
   *   job_step_result MUST be monotonic within a single coordination
   *   loop.  Once a job's init() or step() returns a "falsy" result
   *   (indicating completion), it must never return a "truthy" result
   *   in subsequent calls. Failure to adhere to this leads to
   *   undefined behavior during range compaction.
   */
  template <typename J>
  concept job = std::move_constructible<J> && requires(J& job) {
    { job.init() } -> job_step_result;
    { job.step() } -> job_step_result;
  };

  /**
   * @brief Concept for a factory that produces AMAC jobs.
   * @ingroup vault_amac
   */
  template <typename F, typename R, typename I>
  concept job_factory = job<std::invoke_result_t<F, R const&, I>>;

  /**
   * @brief Concept for a reporter that handles completed AMAC jobs.
   * @ingroup vault_amac
   */
  template <typename R, typename J>
  concept job_reporter = job<J> && std::invocable<R, J&&>;
} // namespace vault::amac::concepts

namespace vault::amac {
  /**
   * @brief A fixed-size collection of memory addresses to prefetch.
   * @ingroup vault_amac
   *
   * @tparam N The number of simultaneous probes (e.g., 1 for Binary
   *   Search, 3 for Binary Fuse Filter).
   */
  template <std::size_t N>
  struct job_step_result : public std::array<void const*, N> {
    /**
     * @brief Checks if the job has further work to perform.
     *
     * @return true if at least one pointer in the result is non-null.
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
      return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((*this)[Is] != nullptr) || ...);
      }(std::make_index_sequence<N>{});
    }
  };

  /**
   * @brief Functional coordinator for managing a batch of AMAC jobs.
   * @ingroup vault_amac
   *
   * @tparam N The batch size (interleaving degree). Typical values
   *   are 8-16.
   */
  template <uint8_t N> class coordinator_fn {
    template <concepts::job_step_result J>
    static constexpr void prefetch(J const& step_result)
    {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(step_result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<J>>{});
    }

    /**
     * @brief Internal storage wrapper to manage job lifecycles.
     *
     * Provides manual move-assignment and destruction logic to ensure
     * non-trivially relocatable jobs (like those containing
     * std::string) do not suffer from double-free errors during range
     * compaction.
     *
     * @tparam J The specific job type being stored.
     */
    template <typename J> class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      [[nodiscard]] job_slot() = default;

      job_slot(job_slot const&) = delete;

      job_slot& operator=(job_slot&& other)
      {
        if (this != std::addressof(other)) {
          *this->get() = std::move(*other.get());
        }

        return *this;
      }

      job_slot& operator=(job_slot const&) = delete;

      [[nodiscard]] J* get() noexcept
      {
        return reinterpret_cast<J*>(&storage[0]);
      }
    };

  public:
    /**
     * @brief Executes a batch of jobs using the AMAC algorithm.
     *
     * Coordination happens in three logical phases:

     * 1. **Setup**: Populate the initial batch of N jobs from the
     *   input range and issue first-round prefetches.
     * 2. **Execution/Refill**: Interleave job steps. When a job
     *   completes, it is reported and replaced by a new job from the
     *   input range.
     * 3. **Drain**: Once the input range is exhausted, continue
     *   stepping remaining active jobs until all are complete.
     *
     * @param ijobs The input range of job objects (state machines) to
     *   execute.
     * @param reporter A callable invoked with the Job once it
     *   completes.
     *
     * @pre The ijobs range must be at least an input_range.
     * @pre The value type of the ijobs range must satisfy
     *   vault::amac::concepts::job.
     */
    template <std::ranges::input_range                         Jobs,
      concepts::job_reporter<std::ranges::range_value_t<Jobs>> Reporter>
    static constexpr void operator()(Jobs&& ijobs, Reporter&& reporter)
    {
      using job_t = std::ranges::range_value_t<Jobs>;

      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);

      // Populate up to N jobs from the range of needles and
      // prefetching the memory addresses they will use in the next
      // step.
      auto jobs = std::array<job_slot<job_t>, N>{};

      auto [jobs_first, jobs_last] = std::invoke([&] {
        auto [jobs_first, jobs_last] = std::ranges::subrange(jobs);

        while (jobs_first != jobs_last and ijobs_cursor != ijobs_last) {
          auto&& job = *ijobs_cursor++;

          if (auto addresses = job.init()) {
            prefetch(addresses);
            std::construct_at(
              jobs_first->get(), std::forward<decltype(job)>(job));
            ++jobs_first;
          } else {
            std::invoke(reporter, std::move(job));
          }
        }

        return std::ranges::subrange(std::ranges::begin(jobs), jobs_first);
      });

      // A predicate that determines not only if a job is complete,
      // but also takes the appropriate action depending on the jobs
      // state.
      auto is_inactive = [&](auto& job) {
        if (auto addresses = job.get()->step()) {
          return prefetch(addresses), false;
        } else {
          return std::invoke(reporter, std::move(*job.get())), true;
        }
      };

      // Loop over the active jobs. Remove any that are complete and
      // replace them with new jobs constructed from the remaining
      // needles. Repeat until all needles are consumed.
      auto jobs_cursor = std::remove_if(jobs_first, jobs_last, is_inactive);

      do {
        while (jobs_cursor != jobs_last && ijobs_cursor != ijobs_last) {
          auto&& job = *ijobs_cursor++;

          if (auto addresses = job.init()) {
            prefetch(addresses);
            *jobs_cursor->get() = std::forward<decltype(job)>(job);
            ++jobs_cursor;
          } else {
            std::invoke(reporter, std::forward<decltype(job)>(job));
          }
        }

        jobs_cursor = std::remove_if(jobs_first, jobs_cursor, is_inactive);
      } while (ijobs_cursor != ijobs_last);

      // Once the needles are consumed, step active jobs until they
      // are all complete.
      while (jobs_cursor != jobs_first) {
        jobs_cursor = std::remove_if(jobs_first, jobs_cursor, is_inactive);
      }

      // We manutally constructed the jobs, so we must manually
      // destroy them.
      for (; jobs_first != jobs_last; ++jobs_first) {
        std::destroy_at(jobs_first->get());
      }
    }
  };

  /**
   * @brief Global instance of the coordinator.
   * @ingroup vault_amac
   *
   * Usage: `vault::amac::coordinator<16>(haystack, needles, factory,
   * reporter);`
   *
   * @tparam N The batch size (interleaving degree).
   */
  template <uint8_t N>
  constexpr inline auto const coordinator = coordinator_fn<N>{};
} // namespace vault::amac

template <std::size_t N>
struct std::tuple_size<vault::amac::job_step_result<N>> {
  static constexpr inline auto const value = std::size_t{N};
};

template <std::size_t I, std::size_t N>
struct std::tuple_element<I, vault::amac::job_step_result<N>> {
  using type = void const*;
};

#endif
