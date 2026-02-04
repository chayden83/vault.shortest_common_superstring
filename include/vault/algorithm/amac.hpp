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
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
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

  // --- Chaining / Composition Support ---

  namespace detail {
    // Lifts a step_result<Small> to step_result<Big> by zero-padding.
    template <std::size_t Big, std::size_t Small>
    constexpr step_result<Big> lift_result(step_result<Small> const& src)
    {
      static_assert(Big >= Small, "Cannot lift to a smaller fanout");
      step_result<Big> dst{}; // Zero-initialize (nullptrs)
      for (std::size_t i = 0; i < Small; ++i) {
        dst[i] = src[i];
      }
      return dst;
    }
  } // namespace detail

  /**
   * @brief A composite context that executes ContextA, then transitions to
   * ContextB.
   * * @tparam CtxA The initial context type.
   * @tparam CtxB The subsequent context type.
   * @tparam TransitionFn A callable: (CtxA::Job&&) -> std::optional<CtxB::Job>.
   * @tparam JobA Valid job type for CtxA.
   * @tparam JobB Valid job type for CtxB.
   */
  template <typename CtxA,
    typename CtxB,
    typename TransitionFn,
    typename JobA,
    typename JobB>
  class chained_context {
    CtxA         m_a;
    CtxB         m_b;
    TransitionFn m_trans;

  public:
    // The composite job state is a sum type of the sub-states.
    using job_t = std::variant<JobA, JobB>;

    // The composite fanout is the max of sub-fanouts to ensure return type
    // consistency.
    static constexpr std::size_t fanout()
    {
      return std::max(CtxA::fanout(), CtxB::fanout());
    }

    using result_t = step_result<fanout()>;

    chained_context(CtxA a, CtxB b, TransitionFn t)
        : m_a(std::move(a))
        , m_b(std::move(b))
        , m_trans(std::move(t))
    {}

    // Init: Dispatch to the correct sub-context
    template <typename Emit> result_t init(job_t& j, Emit&& emit)
    {
      if (std::holds_alternative<JobA>(j)) {
        return detail::lift_result<fanout()>(m_a.init(std::get<JobA>(j), emit));
      } else {
        return detail::lift_result<fanout()>(m_b.init(std::get<JobB>(j), emit));
      }
    }

    // Step: Run A. If A finishes, try Transition to B. If B, Run B.
    template <typename Emit> result_t step(job_t& j, Emit&& emit)
    {
      if (std::holds_alternative<JobA>(j)) {
        auto& state_a = std::get<JobA>(j);
        auto  res_a   = m_a.step(state_a, emit);

        if (static_cast<bool>(res_a)) {
          // A is still active
          return detail::lift_result<fanout()>(res_a);
        }

        // A is done. Attempt Transition.
        // We move state_a out to the transition function.
        if (auto next_b = m_trans(std::move(state_a))) {
          // Transition successful: Switch variant to B
          j.template emplace<JobB>(std::move(*next_b));

          // Immediately init B to avoid a pipeline bubble
          return detail::lift_result<fanout()>(
            m_b.init(std::get<JobB>(j), emit));
        } else {
          // Transition declined (chain complete).
          return detail::lift_result<fanout()>(
            res_a); // Returns nullptrs (Done)
        }
      } else {
        // We are in State B
        return detail::lift_result<fanout()>(m_b.step(std::get<JobB>(j), emit));
      }
    }
  };

  /**
   * @brief Helper to create a chained context.
   * * Usage: auto ctx = chain<JobA, JobB>(ctx_a, ctx_b, transition_fn);
   */
  template <typename JobA,
    typename JobB,
    typename CtxA,
    typename CtxB,
    typename TransitionFn>
  constexpr auto chain(CtxA a, CtxB b, TransitionFn t)
  {
    return chained_context<CtxA, CtxB, TransitionFn, JobA, JobB>(
      std::move(a), std::move(b), std::move(t));
  }

  // --- Executor ---

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

      auto               pipeline = array_t{};
      std::vector<job_t> backlog;

      auto emit = [&](
                    job_t&& spawned) { backlog.push_back(std::move(spawned)); };

      auto pipeline_begin  = pipeline.begin();
      auto initialized_end = pipeline.begin();
      auto active_end      = pipeline.begin();

      scope_guard<iter_t> guard{pipeline_begin, initialized_end};

      auto try_activate_into_slot = [&](job_t&& job, iter_t slot) -> bool {
        if (try_execute(report, job, [&] { return ctx.init(job, emit); })) {
          if (slot < initialized_end) {
            *slot->get() = std::move(job);
          } else {
            std::construct_at(slot->get(), std::move(job));
            ++initialized_end;
          }
          return true;
        }
        return false;
      };

      auto is_inactive = [&](auto& slot) {
        return !try_execute(
          report, *slot.get(), [&] { return ctx.step(*slot.get(), emit); });
      };

      while (true) {
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
            break;
          }
        }

        if (active_end == pipeline.begin()) {
          break;
        }

        active_end = std::remove_if(pipeline_begin, active_end, is_inactive);
      }
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
