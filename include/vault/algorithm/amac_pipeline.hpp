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
    constexpr double step_ratio_val      = static_cast<double>(StepRatio::num) / StepRatio::den;

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
   * @brief Aggregate connecting two sub-executors and their transition edge.
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

      // Resolve Payload A type via traits
      using finalize_a_res =
        decltype(std::declval<typename pure_ctx_t::context_a_type&>().finalize(std::declval<job_a_t&>()));
      using payload_a_t = typename type_traits::finalize_traits<finalize_a_res>::payload_type;

      // Resolve Job B type via transition result
      using opt_b_t = std::invoke_result_t<typename pure_ctx_t::transition_fn_type&, job_a_t&, payload_a_t&>;
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

      // Internal routing helper for Stage B completion
      auto finalize_b = [&](auto&& job_b) {
        auto outcome = ctx.ctx_b.finalize(job_b);
        if (outcome.has_value()) {
          if (auto& opt_p = outcome.value(); opt_p.has_value()) {
            std::invoke(reporter, completed, std::forward<decltype(job_b)>(job_b), std::move(*opt_p));
          } else {
            std::invoke(reporter, terminated, std::forward<decltype(job_b)>(job_b));
          }
        } else {
          std::invoke(reporter, failed, std::forward<decltype(job_b)>(job_b), std::move(outcome.error()));
        }
      };

      // Internal routing helper for Stage A completion -> Transition -> Buffer
      auto finalize_a_to_b = [&](auto&& job_a) -> bool {
        if (buffer.full()) {
          return false;
        }

        auto outcome = ctx.ctx_a.finalize(job_a);
        if (outcome.has_value()) {
          if (auto& opt_p = outcome.value(); opt_p.has_value()) {
            auto opt_b = std::invoke(ctx.transition, job_a, *opt_p);
            if (opt_b) {
              buffer.push_back(std::move(*opt_b));
            } else {
              std::invoke(reporter, terminated, std::forward<decltype(job_a)>(job_a));
            }
          } else {
            std::invoke(reporter, terminated, std::forward<decltype(job_a)>(job_a));
          }
        } else {
          std::invoke(reporter, failed, std::forward<decltype(job_a)>(job_a), std::move(outcome.error()));
        }
        return true;
      };

      auto step_b_pred = [&](auto& slot) {
        auto outcome = ctx.ctx_b.step(*slot.get());
        if (outcome.has_value()) {
          if (auto addr = outcome.value()) {
            return prefetch(addr), false;
          }
          finalize_b(std::move(*slot.get()));
          return true;
        }
        std::invoke(reporter, failed, std::move(*slot.get()), std::move(outcome.error()));
        return true;
      };

      auto step_a_pred = [&](auto& slot) {
        auto outcome = ctx.ctx_a.step(*slot.get());
        if (outcome.has_value()) {
          if (auto addr = outcome.value()) {
            return prefetch(addr), false;
          }
          return finalize_a_to_b(std::move(*slot.get()));
        }
        std::invoke(reporter, failed, std::move(*slot.get()), std::move(outcome.error()));
        return true;
      };

      // Phase 1: Setup
      while (cur_a != end_a && in_cur != in_end) {
        if (buffer.full()) {
          break;
        }
        auto&& job     = *in_cur++;
        auto   outcome = ctx.ctx_a.init(job);

        if (outcome.has_value()) {
          if (auto addr = outcome.value()) {
            prefetch(addr);
            std::construct_at(cur_a->get(), std::forward<decltype(job)>(job));
            ++cur_a;
          } else {
            finalize_a_to_b(std::forward<decltype(job)>(job));
          }
        } else {
          std::invoke(reporter, failed, std::forward<decltype(job)>(job), std::move(outcome.error()));
        }
      }

      // Phase 2: Steady State & Wave Drain
      while (cur_a != window_a.begin() || cur_b != window_b.begin() || !buffer.empty() || in_cur != in_end) {

        // Reverse Step 1: Drain Buffer -> B
        while (cur_b != end_b && !buffer.empty()) {
          auto job_b   = buffer.pop_front();
          auto outcome = ctx.ctx_b.init(job_b);

          if (outcome.has_value()) {
            if (auto addr = outcome.value()) {
              prefetch(addr);
              std::construct_at(cur_b->get(), std::move(job_b));
              ++cur_b;
            } else {
              finalize_b(std::move(job_b));
            }
          } else {
            std::invoke(reporter, failed, std::move(job_b), std::move(outcome.error()));
          }
        }

        // Reverse Step 2: Step B
        cur_b = std::remove_if(window_b.begin(), cur_b, step_b_pred);

        // Reverse Step 3: Step A (respecting backpressure)
        cur_a = std::remove_if(window_a.begin(), cur_a, step_a_pred);

        // Reverse Step 4: Refill A
        while (cur_a != end_a && in_cur != in_end) {
          if (buffer.full()) {
            break;
          }
          auto&& job     = *in_cur++;
          auto   outcome = ctx.ctx_a.init(job);

          if (outcome.has_value()) {
            if (auto addr = outcome.value()) {
              prefetch(addr);
              std::construct_at(cur_a->get(), std::forward<decltype(job)>(job));
              ++cur_a;
            } else {
              finalize_a_to_b(std::forward<decltype(job)>(job));
            }
          } else {
            std::invoke(reporter, failed, std::forward<decltype(job)>(job), std::move(outcome.error()));
          }
        }
      }

      for (; cur_a != window_a.begin(); --cur_a) {
        std::destroy_at(std::prev(cur_a)->get());
      }
      for (; cur_b != window_b.begin(); --cur_b) {
        std::destroy_at(std::prev(cur_b)->get());
      }
    }
  };

  template <std::size_t FanoutBudget = 16, std::size_t BufferMultiplier = 4>
  constexpr inline auto const pipeline_executor = pipeline_executor_fn<FanoutBudget, BufferMultiplier>{};

} // namespace vault::amac

#endif // VAULT_AMAC_PIPELINE_HPP
