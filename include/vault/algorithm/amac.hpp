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
 * @brief A composable, exception-safe software pipelining engine.
 */

namespace vault::amac::concepts {

  template <typename T>
  concept step_result = std::constructible_from<bool, T> &&
    []<std::size_t... Is>(std::index_sequence<Is...>) {
      return (std::same_as<void const*, std::tuple_element_t<Is, T>> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});

  template <typename J>
  concept job = std::movable<J>;

  /**
   * @brief Context concept.
   */
  template <typename C, typename J>
  concept context = job<J> && requires(C& ctx, J& job) {
    { C::fanout() } -> std::convertible_to<std::size_t>;
    {
      ctx.init(job, [](auto&&) {})
    } -> step_result;
    {
      ctx.step(job, [](auto&&) {})
    } -> step_result;
  };

  /**
   * @brief Reporter concept.
   */
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

  // --- Implementation Details ---

  namespace detail {
    template <std::size_t Big, std::size_t Small>
    constexpr step_result<Big> lift_result(step_result<Small> const& src)
    {
      static_assert(Big >= Small, "Cannot lift to a smaller fanout");
      step_result<Big> dst{};
      for (std::size_t i = 0; i < Small; ++i) {
        dst[i] = src[i];
      }
      return dst;
    }

    template <std::size_t N, typename Op>
    constexpr decltype(auto) dispatch_index(std::size_t i, Op&& op)
    {
      return
        [&]<std::size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
          using ResultT = decltype(op.template operator()<0>());
          static constexpr ResultT (*table[])(Op&&) = {
            [](Op&& o) { return o.template operator()<Is>(); }...};
          return table[i](std::forward<Op>(op));
        }(std::make_index_sequence<N>{});
    }
  } // namespace detail

  // --- Pipeline ---

  template <typename ContextTuple, typename TransitionTuple>
  class pipeline_context {
    [[no_unique_address]] ContextTuple    m_contexts;
    [[no_unique_address]] TransitionTuple m_transitions;

    template <typename Tuple> struct tuple_to_variant;

    template <typename... Cs> struct tuple_to_variant<std::tuple<Cs...>> {
      using type = std::variant<typename Cs::job_t...>;
    };

  public:
    using job_t = typename tuple_to_variant<ContextTuple>::type;

    static constexpr std::size_t fanout()
    {
      return std::apply(
        [](auto const&... ctxs) { return std::max({ctxs.fanout()...}); },
        ContextTuple{});
    }

    using result_t                          = step_result<fanout()>;
    static constexpr std::size_t num_stages = std::tuple_size_v<ContextTuple>;

    pipeline_context(ContextTuple ctxs, TransitionTuple trans)
        : m_contexts(std::move(ctxs))
        , m_transitions(std::move(trans))
    {}

    template <typename Emit> result_t init(job_t& j, Emit&& emit)
    {
      return detail::dispatch_index<num_stages>(
        j.index(), [&]<std::size_t I>() {
          return detail::lift_result<fanout()>(
            std::get<I>(m_contexts).init(std::get<I>(j), emit));
        });
    }

    template <typename Emit> result_t step(job_t& j, Emit&& emit)
    {
      return detail::dispatch_index<num_stages>(
        j.index(), [&]<std::size_t I>() {
          auto& ctx = std::get<I>(m_contexts);
          auto& job = std::get<I>(j);

          auto res = ctx.step(job, emit);

          if (static_cast<bool>(res)) {
            return detail::lift_result<fanout()>(res);
          }

          if constexpr (I < std::tuple_size_v<TransitionTuple>) {
            auto& transition_fn = std::get<I>(m_transitions);

            if (auto next_job = transition_fn(std::move(job))) {
              j.template emplace<I + 1>(std::move(*next_job));
              return detail::lift_result<fanout()>(
                std::get<I + 1>(m_contexts).init(std::get<I + 1>(j), emit));
            }
          }

          return detail::lift_result<fanout()>(res);
        });
    }

    auto& contexts() && { return m_contexts; }

    auto& transitions() && { return m_transitions; }
  };

  template <typename T> struct is_pipeline : std::false_type {};

  template <typename C, typename T>
  struct is_pipeline<pipeline_context<C, T>> : std::true_type {};

  template <typename Left, typename Right, typename TransitionFn>
  auto chain(Left left, Right right, TransitionFn trans)
  {
    if constexpr (is_pipeline<std::decay_t<Left>>::value) {
      auto combined_ctxs = std::tuple_cat(
        std::move(left).contexts(), std::make_tuple(std::move(right)));
      auto combined_trans = std::tuple_cat(
        std::move(left).transitions(), std::make_tuple(std::move(trans)));
      return pipeline_context(
        std::move(combined_ctxs), std::move(combined_trans));
    } else {
      auto ctx_tuple   = std::make_tuple(std::move(left), std::move(right));
      auto trans_tuple = std::make_tuple(std::move(trans));
      return pipeline_context(std::move(ctx_tuple), std::move(trans_tuple));
    }
  }

  // --- Executor ---

  template <uint8_t     TotalFanout   = 16,
    double_fault_policy FailurePolicy = double_fault_policy::terminate,
    typename ProtoAllocator           = std::allocator<std::byte>>
  class executor_fn {

    // -- Private Helper: Prefetch --
    template <concepts::step_result R>
    static constexpr void prefetch(R const& result)
    {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<R>>{});
    }

    // -- Safety Shim --
    template <typename Reporter> struct safety_shim {
      Reporter& report;

      void fail(auto&& job, std::exception_ptr e)
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

      void complete(auto&& job)
      {
        try {
          report.on_completion(std::move(job));
        } catch (...) {
          fail(std::move(job), std::current_exception());
        }
      }

      template <typename Job, typename Action>
      bool try_execute(Job& job, Action&& action)
      {
        auto on_active = [&](auto&& result) {
          executor_fn::prefetch(result);
          return true;
        };
        auto on_done = [&]() {
          complete(std::move(job));
          return false;
        };

        if constexpr (noexcept(action())) {
          if (auto result = action()) {
            return on_active(result);
          }
          return on_done();
        } else {
          try {
            if (auto result = action()) {
              return on_active(result);
            }
            return on_done();
          } catch (...) {
            fail(std::move(job), std::current_exception());
            return false;
          }
        }
      }
    };

    // -- Internal Storage --

    template <typename J> class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      using value_type                     = J;
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

    // -- Emitter Functor --
    template <typename BacklogType> struct emitter {
      using value_type = typename BacklogType::value_type;
      BacklogType& target;

      void operator()(value_type&& j) const { target.push_back(std::move(j)); }

      void operator()(std::initializer_list<value_type> list) const
        requires std::copy_constructible<value_type>
      {
        target.reserve(target.size() + list.size());
        for (const auto& item : list) {
          target.push_back(item);
        }
      }

      template <std::ranges::input_range R = std::initializer_list<value_type>>
        requires std::convertible_to<std::ranges::range_reference_t<R>,
          value_type>
      void operator()(R&& range) const
      {
        if constexpr (std::ranges::sized_range<R>) {
          target.reserve(target.size() + std::ranges::size(range));
        }
        for (auto&& item : range) {
          target.push_back(std::forward<decltype(item)>(item));
        }
      }
    };

  public:
    template <typename Context,
      std::ranges::input_range                             Jobs,
      concepts::reporter<std::ranges::range_value_t<Jobs>> Reporter>
      requires concepts::context<Context, std::ranges::range_value_t<Jobs>>
    static constexpr void operator()(Context& ctx,
      Jobs&&                                  ijobs,
      Reporter&&                              reporter,
      ProtoAllocator                          proto_alloc = ProtoAllocator())
    {
      using job_t  = std::ranges::range_value_t<Jobs>;
      using slot_t = job_slot<job_t>;
      static constexpr auto const PIPELINE_SIZE =
        (TotalFanout + Context::fanout() - 1) / Context::fanout();

      // -- Setup --
      safety_shim<Reporter> safety{reporter};

      // 1. Pipeline State
      using array_t = std::array<slot_t, PIPELINE_SIZE>;
      array_t pipeline;

      auto initialized_end = pipeline.begin();
      auto active_end      = pipeline.begin();
      auto pipeline_begin  = pipeline.begin();

      scope_guard guard{pipeline_begin, initialized_end};

      // 2. Work Queue State
      using JobAllocator = typename std::allocator_traits<
        ProtoAllocator>::template rebind_alloc<job_t>;
      using BacklogVector = std::vector<job_t, JobAllocator>;

      BacklogVector          backlog(proto_alloc);
      emitter<BacklogVector> emit{backlog};

      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);

      // -- Linearized Helpers --

      auto pipeline_full  = [&] { return active_end == pipeline.end(); };
      auto pipeline_empty = [&] { return active_end == pipeline.begin(); };

      // Polls for work from Backlog then Input
      auto fetch_next_job = [&](auto&& consume) -> bool {
        if (!backlog.empty()) {
          consume(std::move(backlog.back()));
          backlog.pop_back();
          return true;
        }
        if (ijobs_cursor != ijobs_last) {
          consume(std::move(*ijobs_cursor));
          ++ijobs_cursor;
          return true;
        }
        return false;
      };

      // Tries to init a job and admit it to the pipeline
      auto activate_job = [&](job_t&& job) {
        auto action = [&]() noexcept(noexcept(ctx.init(job, emit))) {
          return ctx.init(job, emit);
        };

        if (safety.try_execute(job, action)) {
          // Job is active: place in slot
          if (active_end < initialized_end) {
            *active_end->get() = std::move(job); // Recycle
          } else {
            std::construct_at(active_end->get(), std::move(job)); // Extend
            ++initialized_end;
          }
          ++active_end;
        }
      };

      // Predicate for std::remove_if
      auto is_inactive = [&](slot_t& slot) {
        auto& job    = *slot.get();
        auto  action = [&]() noexcept(noexcept(ctx.step(job, emit))) {
          return ctx.step(job, emit);
        };
        return !safety.try_execute(job, action);
      };

      // -- Unified Control Loop --

      while (true) {
        // A. Greedy Refill Phase
        while (!pipeline_full()) {
          bool source_has_work = fetch_next_job(activate_job);
          if (!source_has_work) {
            break;
          }
        }

        // B. Termination Phase
        if (pipeline_empty()) {
          break;
        }

        // C. Execution Phase
        active_end = std::remove_if(pipeline_begin, active_end, is_inactive);
      }
    }
  };

  /**
   * @brief Global instance of the executor.
   */
  template <uint8_t     TotalFanout   = 16,
    double_fault_policy FailurePolicy = double_fault_policy::terminate,
    typename Allocator                = std::allocator<std::byte>>
  constexpr inline auto const executor =
    executor_fn<TotalFanout, FailurePolicy, Allocator>{};

} // namespace vault::amac

template <std::size_t N> struct std::tuple_size<vault::amac::step_result<N>> {
  static constexpr inline auto const value = std::size_t{N};
};

template <std::size_t I, std::size_t N>
struct std::tuple_element<I, vault::amac::step_result<N>> {
  using type = void const*;
};

#endif
