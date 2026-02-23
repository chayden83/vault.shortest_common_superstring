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

  struct pipeline_config {
    std::size_t window_a;
    std::size_t window_b;
  };

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
    constexpr double work_a              = static_cast<double>(FanoutA);
    constexpr double work_b              = static_cast<double>(FanoutB) * transition_prob_val * step_ratio_val;
    constexpr std::size_t initial_budget_remaining = FanoutBudget - (FanoutA + FanoutB);
    constexpr double      budget_a                 = initial_budget_remaining * work_a / (work_a + work_b);
    constexpr double      budget_b                 = initial_budget_remaining * work_b / (work_a + work_b);
    auto                  extra_jobs_a             = static_cast<std::size_t>(budget_a / FanoutA);
    auto                  extra_jobs_b             = static_cast<std::size_t>(budget_b / FanoutB);
    auto                  window_a                 = 1 + extra_jobs_a;
    auto                  window_b                 = 1 + extra_jobs_b;
    double                rem_a = (budget_a / static_cast<double>(FanoutA)) - static_cast<double>(extra_jobs_a);
    double                rem_b = (budget_b / static_cast<double>(FanoutB)) - static_cast<double>(extra_jobs_b);
    auto                  whole_budget_a = window_a * FanoutA;
    auto                  whole_budget_b = window_b * FanoutB;
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

  template <
    typename ContextA,
    typename ContextB,
    typename TransitionFn,
    std::size_t FanoutA,
    std::size_t FanoutB,
    typename StepRatioPolicy,
    typename TransitionProbPolicy>
  struct composed_context {
    using context_a_type                  = ContextA;
    using context_b_type                  = ContextB;
    using transition_fn_type              = TransitionFn;
    using step_ratio                      = StepRatioPolicy;
    using transition_probability          = TransitionProbPolicy;
    static constexpr std::size_t fanout_a = FanoutA;
    static constexpr std::size_t fanout_b = FanoutB;
    ContextA                     ctx_a;
    ContextB                     ctx_b;
    TransitionFn                 transition;

    [[nodiscard]] static constexpr std::size_t fanout() noexcept {
      return FanoutA + FanoutB;
    }
  };

  template <std::size_t FanoutBudget = 16, std::size_t BufferMultiplier = 4>
  class pipeline_executor_fn {
    template <typename J>
    class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      [[nodiscard]] job_slot() = default;

      job_slot& operator=(job_slot&& other) {
        if (this != std::addressof(other)) {
          *this->get() = std::move(*other.get());
        }
        return *this;
      }

      constexpr void compact_from(job_slot& other) {
        if (this != std::addressof(other)) {
          std::construct_at(this->get(), std::move(*other.get()));
          std::destroy_at(other.get());
        }
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
      using payload_a_t =
        typename type_traits::finalize_traits<decltype(std::declval<typename pure_ctx_t::context_a_type&>()
                                                         .finalize(std::declval<job_a_t&>()))>::payload_type;
      using job_b_t =
        typename std::invoke_result_t<typename pure_ctx_t::transition_fn_type&, job_a_t&, payload_a_t&>::value_type;

      constexpr auto config = make_pipeline<
        FanoutBudget,
        typename pure_ctx_t::transition_probability,
        typename pure_ctx_t::step_ratio,
        pure_ctx_t::fanout_a,
        pure_ctx_t::fanout_b>();
      auto window_a = std::array<job_slot<job_a_t>, config.window_a>{};
      auto window_b = std::array<job_slot<job_b_t>, config.window_b>{};
      auto buffer =
        static_circular_buffer<job_b_t, std::max<std::size_t>(config.window_a, config.window_b * BufferMultiplier)>{};
      auto in_cur   = std::ranges::begin(ijobs);
      auto in_end   = std::ranges::end(ijobs);
      auto prefetch = []<typename JR>(JR const& res) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
          (__builtin_prefetch(std::get<Is>(res), 0, 3), ...);
        }(std::make_index_sequence<std::tuple_size_v<JR>>{});
      };

      auto finalize_b = [&](auto&& jb) {
        auto out = ctx.ctx_b.finalize(jb);
        if (out.has_value()) {
          if (auto& opt = out.value(); opt.has_value()) {
            std::invoke(reporter, completed, std::forward<decltype(jb)>(jb), std::move(*opt));
          } else {
            std::invoke(reporter, terminated, std::forward<decltype(jb)>(jb));
          }
        } else {
          std::invoke(reporter, failed, std::forward<decltype(jb)>(jb), std::move(out.error()));
        }
      };

      auto finalize_a_to_b = [&](auto&& ja) -> bool {
        if (buffer.full()) {
          return false;
        }
        auto out = ctx.ctx_a.finalize(ja);
        if (out.has_value()) {
          if (auto& opt = out.value(); opt.has_value()) {
            if (auto ob = std::invoke(ctx.transition, ja, *opt)) {
              buffer.push_back(std::move(*ob));
            } else {
              std::invoke(reporter, terminated, std::forward<decltype(ja)>(ja));
            }
          } else {
            std::invoke(reporter, terminated, std::forward<decltype(ja)>(ja));
          }
        } else {
          std::invoke(reporter, failed, std::forward<decltype(ja)>(ja), std::move(out.error()));
        }
        return true;
      };

      auto refill_a = [&](this auto& self, auto& slot) -> bool {
        if (in_cur == in_end || buffer.full()) {
          return false;
        }
        auto&& j   = *in_cur++;
        auto   out = ctx.ctx_a.init(j);
        if (out.has_value()) {
          if (auto ad = out.value()) {
            prefetch(ad);
            std::construct_at(slot.get(), std::forward<decltype(j)>(j));
            return true;
          }
          if (finalize_a_to_b(std::forward<decltype(j)>(j))) {
            return self(slot);
          }
          return false;
        }
        std::invoke(reporter, failed, std::forward<decltype(j)>(j), std::move(out.error()));
        return self(slot);
      };

      auto refill_b = [&](this auto& self, auto& slot) -> bool {
        if (buffer.empty()) {
          return false;
        }
        auto jb  = buffer.pop_front();
        auto out = ctx.ctx_b.init(jb);
        if (out.has_value()) {
          if (auto ad = out.value()) {
            prefetch(ad);
            std::construct_at(slot.get(), std::move(jb));
            return true;
          }
          finalize_b(std::move(jb));
          return self(slot);
        }
        std::invoke(reporter, failed, std::move(jb), std::move(out.error()));
        return self(slot);
      };

      auto act_a_end = window_a.begin();
      auto act_b_end = window_b.begin();
      for (auto& s : window_a) {
        if (refill_a(s)) {
          ++act_a_end;
        } else {
          break;
        }
      }

      while (act_a_end != window_a.begin() || act_b_end != window_b.begin() || !buffer.empty() || in_cur != in_end) {
        while (act_b_end != window_b.end() && !buffer.empty()) {
          if (refill_b(*act_b_end)) {
            ++act_b_end;
          }
        }
        auto it_b = window_b.begin();
        while (it_b != act_b_end) {
          auto out = ctx.ctx_b.step(*it_b->get());
          if (out.has_value()) {
            if (auto ad = out.value()) {
              prefetch(ad);
              ++it_b;
            } else {
              finalize_b(std::move(*it_b->get()));
              std::destroy_at(it_b->get());
              if (refill_b(*it_b)) {
                ++it_b;
              } else {
                it_b->compact_from(*std::prev(act_b_end));
                --act_b_end;
              }
            }
          } else {
            std::invoke(reporter, failed, std::move(*it_b->get()), std::move(out.error()));
            std::destroy_at(it_b->get());
            if (refill_b(*it_b)) {
              ++it_b;
            } else {
              it_b->compact_from(*std::prev(act_b_end));
              --act_b_end;
            }
          }
        }
        auto it_a = window_a.begin();
        while (it_a != act_a_end) {
          auto out = ctx.ctx_a.step(*it_a->get());
          if (out.has_value()) {
            if (auto ad = out.value()) {
              prefetch(ad);
              ++it_a;
            } else {
              if (finalize_a_to_b(std::move(*it_a->get()))) {
                std::destroy_at(it_a->get());
                if (refill_a(*it_a)) {
                  ++it_a;
                } else {
                  it_a->compact_from(*std::prev(act_a_end));
                  --act_a_end;
                }
              } else {
                ++it_a;
              }
            }
          } else {
            std::invoke(reporter, failed, std::move(*it_a->get()), std::move(out.error()));
            std::destroy_at(it_a->get());
            if (refill_a(*it_a)) {
              ++it_a;
            } else {
              it_a->compact_from(*std::prev(act_a_end));
              --act_a_end;
            }
          }
        }
        while (act_a_end != window_a.end() && !buffer.full() && in_cur != in_end) {
          if (refill_a(*act_a_end)) {
            ++act_a_end;
          }
        }
      }
      for (auto it = window_a.begin(); it != act_a_end; ++it) {
        std::destroy_at(it->get());
      }
      for (auto it = window_b.begin(); it != act_b_end; ++it) {
        std::destroy_at(it->get());
      }
    }
  };

  template <std::size_t FanoutBudget = 16, std::size_t BufferMultiplier = 4>
  constexpr inline auto const pipeline_executor = pipeline_executor_fn<FanoutBudget, BufferMultiplier>{};

} // namespace vault::amac

#endif
