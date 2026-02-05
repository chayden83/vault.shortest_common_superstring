// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

/**
 * @defgroup vault_amac Asynchronous Memory Access Coordinator (AMAC)
 *
 * @brief A software-pipelining engine for hiding memory latency.
 *
 * This updated version separates "State" (the Job) from "Behavior" (the
 * Context).
 *
 * - **Job**: Mutable state unique to a specific task (e.g., current probe
 * index).
 * - **Context**: Immutable environment and logic shared by all jobs (e.g.,
 * hash table base address, step function logic).
 */

namespace vault::amac::concepts {

  /**
   * @brief Validates the return type of context state transitions.
   * @ingroup vault_amac
   *
   * A result must be explicitly convertible to bool (true = active,
   * false = done) and satisfy the tuple protocol where every element
   * is a `void const*`.
   */
  template <typename T>
  concept step_result = std::constructible_from<bool, T> &&
    []<std::size_t... Is>(std::index_sequence<Is...>) {
      return (std::same_as<void const*, std::tuple_element_t<Is, T>> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});

  /**
   * @brief Defines the requirements for a Job State.
   * @ingroup vault_amac
   *
   * A Job is purely a data carrier. It must be movable (constructible and
   * assignable) to support compaction and slot recycling.
   */
  template <typename J>
  concept job = std::movable<J>;

  /**
   * @brief Defines the interface for the execution Context.
   * @ingroup vault_amac
   *
   * The Context encapsulates the behavior and shared immutable state.
   * It acts as the "Visitor" or "Executor" for the Job state.
   */
  template <typename C, typename J>
  concept context = job<J> && requires(C& ctx, J& job) {
    { C::fanout() } -> std::convertible_to<std::size_t>;
    { ctx.init(job) } -> step_result;
    { ctx.step(job) } -> step_result;
  };

  /**
   * @brief Receiver-style interface for handling job outcomes.
   * @ingroup vault_amac
   */
  template <typename R, typename J>
  concept reporter = job<J> && requires(R& r, J&& job, std::exception_ptr e) {
    { r.on_completion(std::move(job)) };
    { r.on_failure(std::move(job), e) };
  };

} // namespace vault::amac::concepts

namespace vault::amac {

  /**
   * @brief Defines behavior when the reporter's on_failure method throws.
   */
  enum class double_fault_policy {
    rethrow,  ///< Propagate the exception (aborts the batch, destroys active
              ///< jobs).
    suppress, ///< Catch and ignore the exception (orphans the failing job).
    terminate ///< Call std::terminate() immediately.
  };

  /**
   * @brief A fixed-size collection of memory addresses to prefetch.
   * @ingroup vault_amac
   *
   * @tparam N The number of simultaneous probes.
   */
  template <std::size_t N>
  struct step_result : public std::array<void const*, N> {
    /**
     * @brief Checks if the job has further work to perform.
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
   * @brief Functional executor for managing a batch of AMAC jobs.
   * @ingroup vault_amac
   *
   * @tparam TotalFanout The interleaving degree. Typical values are 8-16.
   * @tparam FailurePolicy Strategy for handling exceptions thrown by the
   * reporter.
   */
  template <uint8_t     TotalFanout   = 16,
    double_fault_policy FailurePolicy = double_fault_policy::terminate>
  class executor_fn {
    template <concepts::step_result R>
    static constexpr void prefetch(R const& result)
    {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<R>>{});
    }

    /**
     * @brief Internal storage wrapper to manage job lifecycles.
     *
     * Provides manual move-assignment and destruction logic to ensure
     * non-trivially relocatable jobs do not suffer from double-free errors.
     */
    template <typename J> class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      // Expose the underlying value type for introspection by scope_guard
      using value_type = J;

      [[nodiscard]] job_slot() = default;

      job_slot(job_slot const&)            = delete;
      job_slot& operator=(job_slot const&) = delete;

      // Proxies move-assignment to the underlying object
      job_slot& operator=(job_slot&& other)
      {
        if (this != std::addressof(other)) {
          *this->get() = std::move(*other.get());
        }
        return *this;
      }

      [[nodiscard]] J* get() noexcept
      {
        return reinterpret_cast<J*>(&storage[0]);
      }
    };

    /**
     * @brief RAII Guard to clean up active jobs if an exception propagates.
     */
    template <typename Iter> struct scope_guard {
      Iter&       first;
      Iter const& last;

      ~scope_guard()
      {
        using JobType =
          typename std::iterator_traits<Iter>::value_type::value_type;

        if constexpr (!std::is_trivially_destructible_v<JobType>) {
          for (; first != last; ++first) {
            std::destroy_at(first->get());
          }
        }
      }
    };

    /**
     * @brief Handles exceptions thrown by the reporter during failure handling.
     */
    template <typename Reporter, typename Job>
    static void safe_fail(Reporter& report, Job&& job, std::exception_ptr e)
    {
      try {
        report.on_failure(std::move(job), e);
      } catch (...) {
        if constexpr (FailurePolicy == double_fault_policy::terminate) {
          std::terminate();
        } else if constexpr (FailurePolicy == double_fault_policy::rethrow) {
          throw;
        } else {
          // Suppress: do nothing
        }
      }
    }

    /**
     * @brief Wraps completion reporting.
     * If completion reporting fails, it escalates to safe_fail.
     */
    template <typename Reporter, typename Job>
    static void safe_complete(Reporter& report, Job&& job)
    {
      try {
        report.on_completion(std::move(job));
      } catch (...) {
        // Completion failed; treat as a failure.
        safe_fail(report, std::move(job), std::current_exception());
      }
    }

  public:
    /**
     * @brief Executes a batch of jobs using the AMAC algorithm.
     *
     * @param ctx The context defining the behavior and shared state.
     * @param ijobs The input range of job states.
     * @param report A callable adhering to the reporter concept.
     */
    template <typename Context,
      std::ranges::input_range                             Jobs,
      concepts::reporter<std::ranges::range_value_t<Jobs>> Reporter>
      requires concepts::context<Context, std::ranges::range_value_t<Jobs>>
    static constexpr void operator()(
      Context& ctx, Jobs&& ijobs, Reporter&& report)
    {
      using job_t  = std::ranges::range_value_t<Jobs>;
      using slot_t = job_slot<job_t>;
      // Iterator type for the array
      using iter_t = typename std::array<slot_t,
        (TotalFanout + Context::fanout() - 1) / Context::fanout()>::iterator;

      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);

      static constexpr auto const JOB_COUNT =
        (TotalFanout + Context::fanout() - 1) / Context::fanout();

      auto jobs = std::array<slot_t, JOB_COUNT>{};

      // 'jobs_last' tracks the High-Water Mark of initialized/constructed
      // slots. The RAII guard owns the range [jobs.begin(), jobs_last). Even if
      // slots contain moved-from "zombies", they are valid and must be
      // destroyed.
      auto jobs_first = jobs.begin();
      auto jobs_last  = jobs.begin();

      scope_guard<iter_t> guard{jobs_first, jobs_last};

      // Helper to process a job and place it in the pipeline
      auto activate_job = [&](auto&& job, auto slot_iter) {
        try {
          if (auto addresses = ctx.init(job)) {
            prefetch(addresses);

            // If the slot is within the initialized range, we Assign (Recycle).
            // If it is at the boundary, we Construct (Extend).
            if (slot_iter < jobs_last) {
              *slot_iter->get() = std::forward<decltype(job)>(job);
            } else {
              std::construct_at(
                slot_iter->get(), std::forward<decltype(job)>(job));
              ++jobs_last; // Extend RAII scope
            }
            return true; // Kept
          } else {
            safe_complete(report, std::forward<decltype(job)>(job));
            return false; // Dropped
          }
        } catch (...) {
          safe_fail(
            report, std::forward<decltype(job)>(job), std::current_exception());
          return false; // Dropped
        }
      };

      // 1. Setup Phase: Populate initial batch
      // We fill up to JOB_COUNT or until input runs out.
      while (jobs_last != jobs.end() and ijobs_cursor != ijobs_last) {
        // In Setup, slot_iter always equals jobs_last, so we always Construct.
        activate_job(*ijobs_cursor++, jobs_last);
      }

      // Predicate for std::remove_if
      // - Calls ctx.step()
      // - Issues prefetches if active
      // - Reports and returns true (remove) if done
      auto is_inactive = [&](auto& slot) {
        try {
          if (auto addresses = ctx.step(*slot.get())) {
            prefetch(addresses);
            return false;
          } else {
            safe_complete(report, std::move(*slot.get()));
            return true;
          }
        } catch (...) {
          safe_fail(report, std::move(*slot.get()), std::current_exception());
          return true;
        }
      };

      // 'active_end' tracks the boundary of currently running jobs.
      // [jobs.begin(), active_end) are Running.
      // [active_end, jobs_last) are Zombies (moved-from).
      auto active_end = jobs_last;

      // 2. Execution/Refill Phase
      active_end = std::remove_if(jobs_first, active_end, is_inactive);

      do {
        // Refill loop: Use holes (zombies) first, then extend if space permits.
        while (active_end != jobs.end() && ijobs_cursor != ijobs_last) {
          if (activate_job(*ijobs_cursor++, active_end)) {
            ++active_end;
          }
        }

        active_end = std::remove_if(jobs_first, active_end, is_inactive);

      } while (ijobs_cursor != ijobs_last);

      // 3. Drain Phase
      while (active_end != jobs_first) {
        active_end = std::remove_if(jobs_first, active_end, is_inactive);
      }

      // Scope Guard handles destruction of [jobs_first, jobs_last).
      // This includes any zombies left behind by the final remove_if.
    }
  };

  /**
   * @brief Global instance of the executor.
   */
  template <uint8_t     TotalFanout   = 16,
    double_fault_policy FailurePolicy = double_fault_policy::terminate>
  constexpr inline auto const executor =
    executor_fn<TotalFanout, FailurePolicy>{};

} // namespace vault::amac

// Tuple protocol support for step_result
template <std::size_t N> struct std::tuple_size<vault::amac::step_result<N>> {
  static constexpr inline auto const value = std::size_t{N};
};

template <std::size_t I, std::size_t N>
struct std::tuple_element<I, vault::amac::step_result<N>> {
  using type = void const*;
};

#endif
