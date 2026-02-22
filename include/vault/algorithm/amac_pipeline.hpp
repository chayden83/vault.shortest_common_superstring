// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_PIPELINE_HPP
#define VAULT_AMAC_PIPELINE_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <ratio>
#include <tuple>
#include <type_traits>
#include <utility>

#include <vault/algorithm/amac.hpp>

namespace vault::amac {

  /**
   * @brief Configuration bounds for an AMAC pipeline.
   */
  struct pipeline_config {
    std::size_t window_a;
    std::size_t window_b;
  };

  /**
   * @brief Computes optimal active window sizes for a composed pipeline.
   *
   * @tparam FanoutBudget The maximum number of concurrent prefetches allowed.
   * @tparam TransitionProbability A std::ratio representing the probability of a job transitioning.
   * @tparam StepRatio A std::ratio representing the expected steps in B vs A (s_b / s_a).
   * @tparam FanoutA The fanout of Stage A jobs.
   * @tparam FanoutB The fanout of Stage B jobs.
   */
  template <
    std::size_t FanoutBudget = 16,
    typename TransitionProbability,
    typename StepRatio,
    std::size_t FanoutA,
    std::size_t FanoutB>
  [[nodiscard]] consteval auto make_pipeline() -> pipeline_config {
    static_assert(FanoutA > 0 && FanoutB > 0, "Fanouts must be > 0");
    static_assert(FanoutBudget >= FanoutA + FanoutB, "Budget too small for baseline");

    constexpr double transition_prob_val = static_cast<double>(TransitionProbability::num) / TransitionProbability::den;

    constexpr double step_ratio_val = static_cast<double>(StepRatio::num) / StepRatio::den;

    constexpr double work_a = static_cast<double>(FanoutA);
    constexpr double work_b = static_cast<double>(FanoutB) * transition_prob_val * step_ratio_val;

    constexpr std::size_t initial_budget_remaining = FanoutBudget - (FanoutA + FanoutB);

    constexpr double budget_a = initial_budget_remaining * work_a / (work_a + work_b);
    constexpr double budget_b = initial_budget_remaining * work_b / (work_a + work_b);

    auto extra_jobs_a = static_cast<std::size_t>(budget_a / FanoutA);
    auto extra_jobs_b = static_cast<std::size_t>(budget_b / FanoutB);

    auto window_a = 1 + extra_jobs_a;
    auto window_b = 1 + extra_jobs_b;

    double rem_a = (budget_a / static_cast<double>(FanoutA)) - static_cast<double>(extra_jobs_a);
    double rem_b = (budget_b / static_cast<double>(FanoutB)) - static_cast<double>(extra_jobs_b);

    auto whole_budget_a = window_a * FanoutA;
    auto whole_budget_b = window_b * FanoutB;

    if (whole_budget_a + whole_budget_b >= FanoutBudget) {
      return pipeline_config{window_a, window_b};
    }

    auto loop_budget_remaining = FanoutBudget - whole_budget_a - whole_budget_b;

    while (loop_budget_remaining != 0) {
      if (loop_budget_remaining < FanoutA && loop_budget_remaining < FanoutB) {
        break;
      } else if (loop_budget_remaining < FanoutA) {
        while (loop_budget_remaining >= FanoutB) {
          window_b += 1;
          loop_budget_remaining -= FanoutB;
        }
      } else if (loop_budget_remaining < FanoutB) {
        while (loop_budget_remaining >= FanoutA) {
          window_a += 1;
          loop_budget_remaining -= FanoutA;
        }
      } else {
        if (rem_a > rem_b) {
          window_a += 1;
          loop_budget_remaining -= FanoutA;
          rem_a = 0.0;
        } else {
          window_b += 1;
          loop_budget_remaining -= FanoutB;
          rem_b = 0.0;
        }
      }
    }

    return pipeline_config{window_a, window_b};
  }

  /**
   * @brief A strictly stack-allocated, zero-dependency ring buffer.
   *
   * @tparam T The type of elements stored. Must be move-constructible.
   * @tparam Capacity The maximum number of elements the buffer can hold.
   */
  template <typename T, std::size_t Capacity>
  class static_circular_buffer {
    static_assert(Capacity > 0, "Circular buffer capacity must be non-zero.");

    alignas(T) std::array<std::byte, sizeof(T) * Capacity> storage_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};

    [[nodiscard]] constexpr T* ptr(std::size_t index) noexcept {
      return reinterpret_cast<T*>(&storage_[index * sizeof(T)]);
    }

  public:
    [[nodiscard]] constexpr static_circular_buffer() = default;

    constexpr ~static_circular_buffer() {
      while (!empty()) {
        std::destroy_at(ptr(head_));
        head_ = (head_ + 1) % Capacity;
        --size_;
      }
    }

    static_circular_buffer(static_circular_buffer const&)            = delete;
    static_circular_buffer& operator=(static_circular_buffer const&) = delete;

    [[nodiscard]] constexpr bool empty() const noexcept {
      return size_ == 0;
    }

    [[nodiscard]] constexpr bool full() const noexcept {
      return size_ == Capacity;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
      return size_;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
      return Capacity;
    }

    constexpr void push_back(T&& value) {
      assert(!full());
      std::construct_at(ptr(tail_), std::move(value));
      tail_ = (tail_ + 1) % Capacity;
      ++size_;
    }

    constexpr auto pop_front() -> T {
      assert(!empty());
      T* p     = ptr(head_);
      T  value = std::move(*p);
      std::destroy_at(p);
      head_ = (head_ + 1) % Capacity;
      --size_;
      return value;
    }
  };

  /**
   * @brief An aggregate connecting two sub-executors and their transition edge.
   */
  template <
    typename ContextA,
    typename ContextB,
    typename TransitionFn,
    std::size_t FanoutA,
    std::size_t FanoutB,
    typename StepRatioPolicy,
    typename TransitionProbPolicy>
  struct composed_context {
    using context_a_type         = ContextA;
    using context_b_type         = ContextB;
    using transition_fn_type     = TransitionFn;
    using step_ratio             = StepRatioPolicy;
    using transition_probability = TransitionProbPolicy;

    static constexpr std::size_t fanout_a = FanoutA;
    static constexpr std::size_t fanout_b = FanoutB;

    ContextA     ctx_a;
    ContextB     ctx_b;
    TransitionFn transition;

    [[nodiscard]] static constexpr std::size_t fanout() noexcept {
      return FanoutA + FanoutB;
    }
  };

  /**
   * @brief Functional executor for managing a composed AMAC pipeline.
   *
   * @tparam FanoutBudget The maximum hardware prefetch concurrency.
   * @tparam BufferMultiplier The scale factor for the intermediate circular buffer.
   */
  template <std::size_t FanoutBudget = 16, std::size_t BufferMultiplier = 4>
  class pipeline_executor_fn {
    template <typename J>
    class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      [[nodiscard]] job_slot()             = default;
      job_slot(job_slot const&)            = delete;
      job_slot& operator=(job_slot const&) = delete;

      job_slot& operator=(job_slot&& other) {
        if (this != std::addressof(other)) {
          *this->get() = std::move(*other.get());
        }
        return *this;
      }

      [[nodiscard]] J* get() noexcept {
        return reinterpret_cast<J*>(&storage[0]);
      }
    };

  public:
    template <std::ranges::input_range Jobs, typename ComposedCtx, typename Reporter>
    static constexpr void operator()(Jobs&& ijobs, ComposedCtx&& ctx, Reporter&& reporter) {
      using pure_ctx_t = std::remove_cvref_t<ComposedCtx>;
      using job_a_t    = std::ranges::range_value_t<Jobs>;
      using opt_b_t    = std::invoke_result_t<
           typename pure_ctx_t::transition_fn_type&,
           typename pure_ctx_t::context_a_type&,
           typename pure_ctx_t::context_b_type&,
           job_a_t&>;
      using job_b_t = typename opt_b_t::value_type;

      constexpr auto config = make_pipeline<
        FanoutBudget,
        typename pure_ctx_t::transition_probability,
        typename pure_ctx_t::step_ratio,
        pure_ctx_t::fanout_a,
        pure_ctx_t::fanout_b>();

      constexpr std::size_t n_a   = config.window_a;
      constexpr std::size_t n_b   = config.window_b;
      constexpr std::size_t n_buf = std::max<std::size_t>(n_a, n_b * BufferMultiplier);

      auto window_a = std::array<job_slot<job_a_t>, n_a>{};
      auto window_b = std::array<job_slot<job_b_t>, n_b>{};
      auto buffer   = static_circular_buffer<job_b_t, n_buf>{};

      auto cur_a = window_a.begin();
      auto cur_b = window_b.begin();
      auto end_a = window_a.end();
      auto end_b = window_b.end();

      auto in_cur = std::ranges::begin(ijobs);
      auto in_end = std::ranges::end(ijobs);

      auto prefetch = []<typename JResult>(JResult const& res) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
          (__builtin_prefetch(std::get<Is>(res), 0, 3), ...);
        }(std::make_index_sequence<std::tuple_size_v<JResult>>{});
      };

      auto step_b_pred = [&](auto& slot) {
        if (auto addr = ctx.ctx_b.step(*slot.get())) {
          return prefetch(addr), false;
        } else {
          std::invoke(reporter, std::move(*slot.get()));
          return true;
        }
      };

      auto step_a_pred = [&](auto& slot) {
        if (auto addr = ctx.ctx_a.step(*slot.get())) {
          return prefetch(addr), false;
        } else {
          if (buffer.full()) {
            return false; // Safely stall this job while permitting others in A to step
          }
          auto opt_b = std::invoke(ctx.transition, ctx.ctx_a, ctx.ctx_b, *slot.get());
          if (opt_b) {
            buffer.push_back(std::move(*opt_b));
          } else {
            std::invoke(reporter, std::move(*slot.get()));
          }
          return true;
        }
      };

      // Phase 1: Setup
      while (cur_a != end_a && in_cur != in_end) {
        auto&& job = *in_cur++;
        if (auto addr = ctx.ctx_a.init(job)) {
          prefetch(addr);
          std::construct_at(cur_a->get(), std::forward<decltype(job)>(job));
          ++cur_a;
        } else {
          std::invoke(reporter, std::move(job));
        }
      }

      // Phase 2: Steady State & Wave Drain
      while (cur_a != window_a.begin() || cur_b != window_b.begin() || !buffer.empty() || in_cur != in_end) {

        // Reverse Step 1: Drain Buffer -> B
        while (cur_b != end_b && !buffer.empty()) {
          auto job_b = buffer.pop_front();
          if (auto addr = ctx.ctx_b.init(job_b)) {
            prefetch(addr);
            std::construct_at(cur_b->get(), std::move(job_b));
            ++cur_b;
          } else {
            std::invoke(reporter, std::move(job_b));
          }
        }

        // Reverse Step 2: Step B
        cur_b = std::remove_if(window_b.begin(), cur_b, step_b_pred);

        // Reverse Step 3: Step A (respecting backpressure)
        cur_a = std::remove_if(window_a.begin(), cur_a, step_a_pred);

        // Reverse Step 4: Refill A
        while (cur_a != end_a && in_cur != in_end) {
          auto&& job = *in_cur++;
          if (auto addr = ctx.ctx_a.init(job)) {
            prefetch(addr);
            std::construct_at(cur_a->get(), std::forward<decltype(job)>(job));
            ++cur_a;
          } else {
            std::invoke(reporter, std::move(job));
          }
        }
      }
    }
  };

  template <std::size_t FanoutBudget = 16, std::size_t BufferMultiplier = 4>
  constexpr inline auto const pipeline_executor = pipeline_executor_fn<FanoutBudget, BufferMultiplier>{};

} // namespace vault::amac

#endif // VAULT_AMAC_PIPELINE_HPP
