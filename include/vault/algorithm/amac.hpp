// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <algorithm>
#include <any>
#include <cassert>
#include <concepts>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <ratio>
#include <tuple>
#include <type_traits>
#include <utility>

/**
 * @defgroup vault_amac Asynchronous Memory Access Executor (AMAC)
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
 * The executor manages up to $N$ active jobs
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
 * $N$ should be small (e.g., 2–4). The overhead of the executor
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

namespace vault::amac {
  constexpr inline struct completed_tag {
  } const completed;

  constexpr inline struct terminated_tag {
  } const terminated;

  constexpr inline struct failed_tag {
  } const failed;
} // namespace vault::amac

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
  concept step_result = std::constructible_from<bool, T> && []<std::size_t... Is>(std::index_sequence<Is...>) {
    return (std::same_as<void const*, std::tuple_element_t<Is, T>> && ...);
  }(std::make_index_sequence<std::tuple_size_v<T>>{});
} // namespace vault::amac::concepts

namespace vault::amac::type_traits {
  template <typename T>
  struct is_step_outcome : std::false_type {};

  template <typename S, typename E>
    requires concepts::step_result<S>
  struct is_step_outcome<std::expected<S, E>> : std::true_type {};

  template <typename T>
  constexpr inline auto const is_step_outcome_v = is_step_outcome<T>::value;

  template <typename T>
  struct is_finalize_outcome : std::false_type {};

  template <typename P, typename E>
  struct is_finalize_outcome<std::expected<std::optional<P>, E>> : std::true_type {};

  template <typename T>
  constexpr inline auto const is_finalize_outcome_v = is_finalize_outcome<std::remove_cvref_t<T>>::value;

  template <typename T>
  struct finalize_traits;

  template <typename P, typename E>
  struct finalize_traits<std::expected<std::optional<P>, E>> {
    using payload_type = P;
    using error_type   = E;
  };

} // namespace vault::amac::type_traits

namespace vault::amac::concepts {
  template <typename T>
  concept step_outcome = type_traits::is_step_outcome_v<T>;

  template <typename T>
  concept finalize_outcome = type_traits::is_finalize_outcome_v<T>;

  /**
   * @brief Defines the interface for an AMAC-compatible job.
   * @ingroup vault_amac
   *
   * A Job is a state machine that transitions via init() and step(),
   * and completes via finalize().
   *
   * @note **State Monotonicity Requirement**: The predicate result of
   * job_step_result MUST be monotonic within a single coordination
   * loop.  Once a job's init() or step() returns a "falsy" result
   * (indicating completion), it must never return a "truthy" result
   * in subsequent calls. Failure to adhere to this leads to
   * undefined behavior during range compaction.
   */
  template <typename C, typename J>
  concept job_context = std::move_constructible<J> && C::fanout() > 0uz && requires(C& context, J& job) {
    { context.init(job) } noexcept -> step_outcome;
    { context.step(job) } noexcept -> step_outcome;
    { context.finalize(job) } noexcept -> finalize_outcome;
  };

  /**
   * @brief Concept for a reporter that handles AMAC job lifecycle events.
   *
   * @tparam R The type of the job reporter.
   * @tparam J The type of the job to report.
   * @tparam P The type of the payload returned upon success.
   * @tparam E The type of the error returned by the context when a
   * job fails.
   *
   * @ingroup vault_amac
   */
  template <typename R, typename J, typename P, typename E>
  concept job_reporter = std::invocable<R, completed_tag, J&&, P&&> &&
                         std::invocable<R, terminated_tag, J&&> &&
                         std::invocable<R, failed_tag, J&&, E&&>;
} // namespace vault::amac::concepts

namespace vault::amac {
  /**
   * @brief A fixed-size collection of memory addresses to prefetch.
   * @ingroup vault_amac
   *
   * @tparam N The number of simultaneous probes (e.g., 1 for Binary
   * Search, 3 for Binary Fuse Filter).
   */
  template <std::size_t N>
  struct step_result : public std::array<void const*, N> {
    /**
     * @brief Checks if the job has further work to perform.
     *
     * @return true if at least one pointer in the result is non-null.
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
      return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((*this)[Is] != nullptr) || ...);
      }(std::make_index_sequence<N>{});
    }
  };

  /**
   * @brief Functional executor for managing a batch of AMAC jobs.
   * @ingroup vault_amac
   *
   * @tparam TotalFanout The interleaving degree. Typical values are
   * 8-16.
   */
  template <uint8_t TotalFanout = 16>
  class executor_fn {
    template <concepts::step_result J>
    static constexpr void prefetch(J const& step_result) {
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
    template <typename J>
    class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      [[nodiscard]] job_slot() = default;

      job_slot(job_slot const&) = delete;

      job_slot& operator=(job_slot&& other) {
        if (this != std::addressof(other)) {
          *this->get() = std::move(*other.get());
        }

        return *this;
      }

      job_slot& operator=(job_slot const&) = delete;

      [[nodiscard]] J* get() noexcept {
        return reinterpret_cast<J*>(&storage[0]);
      }
    };

  public:
    /**
     * @brief Executes a batch of jobs using the AMAC algorithm.
     *
     * Coordination happens in three logical phases:

     * 1. **Setup**: Populate the initial batch of N jobs from the
     * input range and issue first-round prefetches.
     * 2. **Execution/Refill**: Interleave job steps. When a job
     * completes, it is reported and replaced by a new job from the
     * input range.
     * 3. **Drain**: Once the input range is exhausted, continue
     * stepping remaining active jobs until all are complete.
     *
     * @param ijobs The input range of job objects (state machines) to
     * execute.
     * @param reporter A callable invoked with the Job once it
     * completes.
     *
     * @pre The ijobs range must be at least an input_range.
     * @pre The value type of the ijobs range must satisfy
     * vault::amac::concepts::job.
     */
    template <std::ranges::input_range Jobs, typename Context, typename Reporter>
      requires concepts::job_context<std::remove_cvref_t<Context>, std::ranges::range_value_t<Jobs>> &&
               concepts::job_reporter<
                 std::remove_cvref_t<Reporter>,
                 std::ranges::range_value_t<Jobs>,
                 typename type_traits::finalize_traits<decltype(
                   std::declval<std::remove_cvref_t<Context>&>().finalize(
                     std::declval<std::ranges::range_value_t<Jobs>&>()))>::payload_type,
                 typename type_traits::finalize_traits<decltype(
                   std::declval<std::remove_cvref_t<Context>&>().finalize(
                     std::declval<std::ranges::range_value_t<Jobs>&>()))>::error_type>
    static constexpr void operator()(Jobs&& ijobs, Context&& context, Reporter&& reporter) {
      using job_t = std::ranges::range_value_t<Jobs>;

      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);

      static constexpr auto const JOB_COUNT = (TotalFanout + context.fanout() - 1) / context.fanout();

      auto jobs = std::array<job_slot<job_t>, JOB_COUNT>{};

      auto finalize_job = [&](auto&& job_to_finalize) {
        auto fin_outcome = context.finalize(job_to_finalize);
        if (fin_outcome.has_value()) {
          if (auto& opt_payload = fin_outcome.value(); opt_payload.has_value()) {
            std::invoke(reporter, completed, std::forward<decltype(job_to_finalize)>(job_to_finalize), std::move(*opt_payload));
          } else {
            std::invoke(reporter, terminated, std::forward<decltype(job_to_finalize)>(job_to_finalize));
          }
        } else {
          std::invoke(reporter, failed, std::forward<decltype(job_to_finalize)>(job_to_finalize), std::move(fin_outcome.error()));
        }
      };

      auto [jobs_first, jobs_last] = std::invoke([&] {
        auto [jobs_first_inner, jobs_last_inner] = std::ranges::subrange(jobs);

        while (jobs_first_inner != jobs_last_inner and ijobs_cursor != ijobs_last) {
          auto&& job = *ijobs_cursor++;

          auto outcome = context.init(job);
          if (outcome.has_value()) {
            if (auto addresses = outcome.value()) {
              prefetch(addresses);
              std::construct_at(jobs_first_inner->get(), std::forward<decltype(job)>(job));
              ++jobs_first_inner;
            } else {
              finalize_job(std::forward<decltype(job)>(job));
            }
          } else {
            std::invoke(reporter, failed, std::forward<decltype(job)>(job), std::move(outcome.error()));
          }
        }

        return std::ranges::subrange(std::ranges::begin(jobs), jobs_first_inner);
      });

      // A predicate that determines not only if a job is complete,
      // but also takes the appropriate action depending on the jobs
      // state.
      auto is_inactive = [&](auto& job_slot) {
        auto outcome = context.step(*job_slot.get());
        if (outcome.has_value()) {
          if (auto addresses = outcome.value()) {
            return prefetch(addresses), false;
          } else {
            finalize_job(std::move(*job_slot.get()));
            return true;
          }
        } else {
          std::invoke(reporter, failed, std::move(*job_slot.get()), std::move(outcome.error()));
          return true;
        }
      };

      // Loop over the active jobs. Remove any that are complete and
      // replace them with new jobs constructed from the remaining
      // needles. Repeat until all needles are consumed.
      auto jobs_cursor = std::remove_if(jobs_first, jobs_last, is_inactive);

      do {
        while (jobs_cursor != jobs_last && ijobs_cursor != ijobs_last) {
          auto&& job = *ijobs_cursor++;

          auto outcome = context.init(job);
          if (outcome.has_value()) {
            if (auto addresses = outcome.value()) {
              prefetch(addresses);
              *jobs_cursor->get() = std::forward<decltype(job)>(job);
              ++jobs_cursor;
            } else {
              finalize_job(std::forward<decltype(job)>(job));
            }
          } else {
            std::invoke(reporter, failed, std::forward<decltype(job)>(job), std::move(outcome.error()));
          }
        }

        jobs_cursor = std::remove_if(jobs_first, jobs_cursor, is_inactive);
      } while (ijobs_cursor != ijobs_last);

      // Once the needles are consumed, step active jobs until they
      // are all complete.
      while (jobs_cursor != jobs_first) {
        jobs_cursor = std::remove_if(jobs_first, jobs_cursor, is_inactive);
      }

      // We manually constructed the jobs, so we must manually
      // destroy them.
      for (; jobs_first != jobs_last; ++jobs_first) {
        std::destroy_at(jobs_first->get());
      }
    }
  }; // namespace vault::amac

  /**
   * @brief Global instance of the executor.
   * @ingroup vault_amac
   *
   * Usage: `vault::amac::executor<16>(haystack, needles, factory,
   * reporter);`
   *
   * @tparam TotalFanout The interleaving degree. Typical values are
   * 8-16.
   */
  template <uint8_t TotalFanout = 16>
  constexpr inline auto const executor = executor_fn<TotalFanout>{};

} // namespace vault::amac

template <std::size_t N>
struct std::tuple_size<vault::amac::step_result<N>> {
  static constexpr inline auto const value = std::size_t{N};
};

template <std::size_t I, std::size_t N>
struct std::tuple_element<I, vault::amac::step_result<N>> {
  using type = void const*;
};

#endif
