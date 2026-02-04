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
 */

namespace vault::amac::concepts {

  template <typename T>
  concept step_result = std::constructible_from<bool, T> &&
    []<std::size_t... Is>(std::index_sequence<Is...>) {
      return (std::same_as<void const*, std::tuple_element_t<Is, T>> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});

  template <typename J>
  concept job = std::movable<J>;

  template <typename C, typename J>
  concept context =
    job<J> && requires(C& ctx, J& job, std::function<void(J&&)> emit) {
      { C::fanout() } -> std::convertible_to<std::size_t>;
      { ctx.init(job, emit) } -> step_result;
      { ctx.step(job, emit) } -> step_result;
    };

  template <typename R, typename J>
  concept reporter = job<J> && requires(R& r, J&& job, std::exception_ptr e) {
    { r.on_completion(std::move(job)) };
    { r.on_failure(std::move(job), e) };
  };

} // namespace vault::amac::concepts

namespace vault::amac {

  enum class double_fault_policy { rethrow, suppress, terminate };

  template <std::size_t N>
  struct step_result : public std::array<void const*, N> {
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
      return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((*this)[Is] != nullptr) || ...);
      }(std::make_index_sequence<N>{});
    }
  };

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

    template <typename J> class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      using value_type = J;

      [[nodiscard]] job_slot()             = default;
      job_slot(job_slot const&)            = delete;
      job_slot& operator=(job_slot const&) = delete;

      // Used for slot recycling (move assignment)
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
     * @brief RAII Guard for the initialized range of the pipeline.
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

    // --- Exception Safety Helpers ---

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

      static constexpr auto const PIPELINE_SIZE =
        (TotalFanout + Context::fanout() - 1) / Context::fanout();
      using array_t = std::array<slot_t, PIPELINE_SIZE>;
      using iter_t  = typename array_t::iterator;

      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);

      // Storage
      auto               pipeline = array_t{};
      std::vector<job_t> backlog; // Spawned jobs (LIFO)

      auto emit = [&](
                    job_t&& spawned) { backlog.push_back(std::move(spawned)); };

      // State Tracking
      auto pipeline_begin = pipeline.begin();
      // Points to one past the last *initialized* slot (High Water Mark)
      auto initialized_end = pipeline.begin();
      // Points to one past the last *active* job (Working Set)
      auto active_end = pipeline.begin();

      // RAII: Owns [pipeline_begin, initialized_end)
      scope_guard<iter_t> guard{pipeline_begin, initialized_end};

      // --- Helpers ---

      // Places a job into the pipeline if it activates successfully
      auto try_activate_into_slot = [&](job_t&& job, iter_t slot) -> bool {
        if (try_execute(report, job, [&] { return ctx.init(job, emit); })) {
          // Success: Job is active.
          if (slot < initialized_end) {
            // Reuse existing slot (Move Assign)
            *slot->get() = std::move(job);
          } else {
            // Initialize new slot (Construct)
            std::construct_at(slot->get(), std::move(job));
            ++initialized_end; // Extend RAII scope
          }
          return true;
        }
        // Job finished or failed immediately; not added to pipeline.
        return false;
      };

      auto is_inactive = [&](auto& slot) {
        return !try_execute(
          report, *slot.get(), [&] { return ctx.step(*slot.get(), emit); });
      };

      // --- Unified Control Loop ---

      while (true) {
        // 1. Refill Phase (Greedy)
        // Fill all available slots up to PIPELINE_SIZE
        while (active_end != pipeline.end()) {
          if (!backlog.empty()) {
            if (try_activate_into_slot(std::move(backlog.back()), active_end)) {
              ++active_end;
            }
            backlog.pop_back();
          } else if (ijobs_cursor != ijobs_last) {
            if (try_activate_into_slot(std::move(*ijobs_cursor), active_end)) {
              ++active_end;
            }
            ++ijobs_cursor;
          } else {
            break; // No sources left
          }
        }

        // 2. Termination Check
        // If pipeline is empty and we couldn't refill it, we are done.
        if (active_end == pipeline.begin()) {
          break;
        }

        // 3. Execution Phase
        // Step all active jobs and compact the array
        active_end = std::remove_if(pipeline_begin, active_end, is_inactive);
      }

      // End of scope: 'guard' destroys [pipeline_begin, initialized_end).
    }
  };

  template <uint8_t     TotalFanout   = 16,
    double_fault_policy FailurePolicy = double_fault_policy::terminate>
  constexpr inline auto const executor =
    executor_fn<TotalFanout, FailurePolicy>{};

} // namespace vault::amac

template <std::size_t N> struct std::tuple_size<vault::amac::step_result<N>> {
  static constexpr inline auto const value = std::size_t{N};
};

template <std::size_t I, std::size_t N>
struct std::tuple_element<I, vault::amac::step_result<N>> {
  using type = void const*;
};

#endif
