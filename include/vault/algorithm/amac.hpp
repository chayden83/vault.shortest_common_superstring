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
#include <vector>

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
   * The Context now supports Dynamic Job Creation via an 'emit' callback.
   * The signature for emit is essentially `void(J&&)`.
   */
  template <typename C, typename J>
  concept context =
    job<J> && requires(C& ctx, J& job, std::function<void(J&&)> emit) {
      { C::fanout() } -> std::convertible_to<std::size_t>;
      { ctx.init(job, emit) } -> step_result;
      { ctx.step(job, emit) } -> step_result;
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
     */
    template <typename J> class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      using value_type = J;

      [[nodiscard]] job_slot() = default;

      job_slot(job_slot const&)            = delete;
      job_slot& operator=(job_slot const&) = delete;

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

    // --- Safety Helpers ---

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

    template <typename Reporter, typename Job>
    static void safe_complete(Reporter& report, Job&& job)
    {
      try {
        report.on_completion(std::move(job));
      } catch (...) {
        safe_fail(report, std::move(job), std::current_exception());
      }
    }

    /**
     * @brief Executes a job step safely.
     * @return true if the job is active (prefetch issued).
     * @return false if the job is done (completed) or failed (exception).
     */
    template <typename Reporter, typename Job, typename Action>
    static bool try_execute(Reporter& report, Job& job, Action&& action)
    {
      try {
        if (auto result = action()) {
          prefetch(result);
          return true;
        } else {
          safe_complete(report, std::move(job));
          return false;
        }
      } catch (...) {
        safe_fail(report, std::move(job), std::current_exception());
        return false;
      }
    }

  public:
    template <typename Context,
      std::ranges::input_range                             Jobs,
      concepts::reporter<std::ranges::range_value_t<Jobs>> Reporter>
      requires concepts::context<Context, std::ranges::range_value_t<Jobs>>
    static constexpr void operator()(
      Context& ctx, Jobs&& ijobs, Reporter&& report)
    {
      using job_t  = std::ranges::range_value_t<Jobs>;
      using slot_t = job_slot<job_t>;
      using iter_t = typename std::array<slot_t,
        (TotalFanout + Context::fanout() - 1) / Context::fanout()>::iterator;

      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);

      static constexpr auto const JOB_COUNT =
        (TotalFanout + Context::fanout() - 1) / Context::fanout();

      auto jobs = std::array<slot_t, JOB_COUNT>{};

      std::vector<job_t> backlog;

      auto emit = [&](
                    job_t&& spawned) { backlog.push_back(std::move(spawned)); };

      auto jobs_first = jobs.begin();
      auto jobs_last  = jobs.begin();

      scope_guard<iter_t> guard{jobs_first, jobs_last};

      // Helper to activate a new job
      // Takes job by forwarding reference.
      // Callers must ensure they std::move() if passing a value intended to be
      // moved.
      auto activate_job = [&](auto&& job, auto slot_iter) {
        if (try_execute(report, job, [&] { return ctx.init(job, emit); })) {
          if (slot_iter < jobs_last) {
            *slot_iter->get() = std::forward<decltype(job)>(job);
          } else {
            std::construct_at(
              slot_iter->get(), std::forward<decltype(job)>(job));
            ++jobs_last;
          }
          return true;
        }
        return false;
      };

      // 1. Setup Phase
      while (jobs_last != jobs.end() and ijobs_cursor != ijobs_last) {
        activate_job(*ijobs_cursor++, jobs_last);
      }

      auto is_inactive = [&](auto& slot) {
        return !try_execute(
          report, *slot.get(), [&] { return ctx.step(*slot.get(), emit); });
      };

      auto active_end = jobs_last;

      // 2. Execution/Refill Phase
      active_end = std::remove_if(jobs_first, active_end, is_inactive);

      do {
        // Refill Logic: Priority Queue (Backlog > Input)
        while (active_end != jobs.end()) {
          if (!backlog.empty()) {
            // 1. Service Backlog
            // Explicitly move from the backlog to satisfy move-only types
            if (activate_job(std::move(backlog.back()), active_end)) {
              ++active_end;
            }
            backlog.pop_back();
          } else if (ijobs_cursor != ijobs_last) {
            // 2. Service Input Range
            if (activate_job(*ijobs_cursor++, active_end)) {
              ++active_end;
            }
          } else {
            break;
          }
        }

        active_end = std::remove_if(jobs_first, active_end, is_inactive);

      } while (ijobs_cursor != ijobs_last || !backlog.empty());

      // 3. Drain Phase
      while (active_end != jobs_first) {
        active_end = std::remove_if(jobs_first, active_end, is_inactive);

        // Refill from backlog ONLY (input is exhausted)
        while (active_end != jobs.end() && !backlog.empty()) {
          if (activate_job(std::move(backlog.back()), active_end)) {
            ++active_end;
          }
          backlog.pop_back();
        }
      }

      // Cleanup handled by scope_guard
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
