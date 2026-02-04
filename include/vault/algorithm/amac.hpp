// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
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
   * A Job is now purely a data carrier. It must be movable so it can
   * travel through the pipeline's slots.
   */
  template <typename J>
  concept job = std::move_constructible<J>;

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
   * @brief Concept for a reporter that handles completed AMAC jobs.
   * @ingroup vault_amac
   */
  template <typename R, typename J>
  concept reporter = job<J> && std::invocable<R, J&&>;

} // namespace vault::amac::concepts

namespace vault::amac {

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
   */
  template <uint8_t TotalFanout = 16> class executor_fn {
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
     * @param ctx The context defining the behavior and shared state.
     * @param ijobs The input range of job states.
     * @param report A callable invoked with the Job once it completes.
     */
    template <typename Context,
      std::ranges::input_range                             Jobs,
      concepts::reporter<std::ranges::range_value_t<Jobs>> Reporter>
      requires concepts::context<Context, std::ranges::range_value_t<Jobs>>
    static constexpr void operator()(
      Context& ctx, Jobs&& ijobs, Reporter&& report)
    {
      using job_t = std::ranges::range_value_t<Jobs>;

      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);

      static constexpr auto const JOB_COUNT =
        (TotalFanout + Context::fanout() - 1) / Context::fanout();

      auto jobs = std::array<job_slot<job_t>, JOB_COUNT>{};

      // 1. Setup Phase: Populate initial batch
      auto [jobs_first, jobs_last] = std::invoke([&] {
        auto [jobs_first, jobs_last] = std::ranges::subrange(jobs);

        while (jobs_first != jobs_last and ijobs_cursor != ijobs_last) {
          auto&& job = *ijobs_cursor++;

          if (auto addresses = ctx.init(job)) {
            prefetch(addresses);
            std::construct_at(
              jobs_first->get(), std::forward<decltype(job)>(job));
            ++jobs_first;
          } else {
            std::invoke(report, std::move(job));
          }
        }

        return std::ranges::subrange(std::ranges::begin(jobs), jobs_first);
      });

      // Predicate for std::remove_if
      // - Calls ctx.step()
      // - Issues prefetches if active
      // - Reports and returns true (remove) if done
      auto is_inactive = [&](auto& slot) {
        if (auto addresses = ctx.step(*slot.get())) {
          return prefetch(addresses), false;
        } else {
          return std::invoke(report, std::move(*slot.get())), true;
        }
      };

      // 2. Execution/Refill Phase
      auto jobs_cursor = std::remove_if(jobs_first, jobs_last, is_inactive);

      do {
        while (jobs_cursor != jobs_last && ijobs_cursor != ijobs_last) {
          auto&& job = *ijobs_cursor++;

          if (auto addresses = ctx.init(job)) {
            prefetch(addresses);
            *jobs_cursor->get() = std::forward<decltype(job)>(job);
            ++jobs_cursor;
          } else {
            std::invoke(report, std::forward<decltype(job)>(job));
          }
        }

        jobs_cursor = std::remove_if(jobs_first, jobs_cursor, is_inactive);
      } while (ijobs_cursor != ijobs_last);

      // 3. Drain Phase
      while (jobs_cursor != jobs_first) {
        jobs_cursor = std::remove_if(jobs_first, jobs_cursor, is_inactive);
      }

      // Cleanup
      if constexpr (!std::is_trivially_destructible_v<job_t>) {
        for (; jobs_first != jobs_last; ++jobs_first) {
          std::destroy_at(jobs_first->get());
        }
      }
    }
  };

  /**
   * @brief Global instance of the executor.
   */
  template <uint8_t TotalFanout = 16>
  constexpr inline auto const executor = executor_fn<TotalFanout>{};

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
