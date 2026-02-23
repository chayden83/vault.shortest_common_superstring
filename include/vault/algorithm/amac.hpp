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

namespace vault::amac {
  constexpr inline struct completed_tag {
  } const completed;

  constexpr inline struct terminated_tag {
  } const terminated;

  constexpr inline struct failed_tag {
  } const failed;
} // namespace vault::amac

namespace vault::amac::concepts {
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

  template <typename C, typename J>
  concept job_context = std::move_constructible<J> && C::fanout() > 0uz && requires(C& context, J& job) {
    { context.init(job) } noexcept -> step_outcome;
    { context.step(job) } noexcept -> step_outcome;
    { context.finalize(job) } noexcept -> finalize_outcome;
  };

  template <typename R, typename J, typename P, typename E>
  concept job_reporter = std::invocable<R, completed_tag, J&&, P&&> && std::invocable<R, terminated_tag, J&&> &&
                         std::invocable<R, failed_tag, J&&, E&&>;
} // namespace vault::amac::concepts

namespace vault::amac {
  template <std::size_t N>
  struct step_result : public std::array<void const*, N> {
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
      return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((*this)[Is] != nullptr) || ...);
      }(std::make_index_sequence<N>{});
    }
  };

  template <uint8_t TotalFanout = 16>
  class executor_fn {
    template <concepts::step_result J>
    static constexpr void prefetch(J const& step_result) {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(step_result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<J>>{});
    }

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

      [[nodiscard]] J* get() noexcept {
        return reinterpret_cast<J*>(&storage[0]);
      }
    };

  public:
    template <std::ranges::input_range Jobs, typename Context, typename Reporter>
      requires concepts::job_context<std::remove_cvref_t<Context>, std::ranges::range_value_t<Jobs>> &&
               concepts::job_reporter<
                 std::remove_cvref_t<Reporter>,
                 std::ranges::range_value_t<Jobs>,
                 typename type_traits::finalize_traits<decltype(std::declval<std::remove_cvref_t<Context>&>().finalize(
                   std::declval<std::ranges::range_value_t<Jobs>&>()
                 ))>::payload_type,
                 typename type_traits::finalize_traits<decltype(std::declval<std::remove_cvref_t<Context>&>().finalize(
                   std::declval<std::ranges::range_value_t<Jobs>&>()
                 ))>::error_type>
    static constexpr void operator()(Jobs&& ijobs, Context&& context, Reporter&& reporter) {
      using job_t                           = std::ranges::range_value_t<Jobs>;
      auto [ijobs_cursor, ijobs_last]       = std::ranges::subrange(ijobs);
      static constexpr auto const JOB_COUNT = (TotalFanout + context.fanout() - 1) / context.fanout();
      auto                        jobs      = std::array<job_slot<job_t>, JOB_COUNT>{};

      auto refill_one = [&](this auto& self, auto& slot) -> bool {
        if (ijobs_cursor == ijobs_last) {
          return false;
        }
        auto&& job     = *ijobs_cursor++;
        auto   outcome = context.init(job);
        if (outcome.has_value()) {
          if (auto addresses = outcome.value()) {
            prefetch(addresses);
            std::construct_at(slot.get(), std::forward<decltype(job)>(job));
            return true;
          }
          auto fin = context.finalize(job);
          if (fin.has_value()) {
            if (auto& opt = fin.value(); opt.has_value()) {
              std::invoke(reporter, completed, std::forward<decltype(job)>(job), std::move(*opt));
            } else {
              std::invoke(reporter, terminated, std::forward<decltype(job)>(job));
            }
          } else {
            std::invoke(reporter, failed, std::forward<decltype(job)>(job), std::move(fin.error()));
          }
          return self(slot);
        }
        std::invoke(reporter, failed, std::forward<decltype(job)>(job), std::move(outcome.error()));
        return self(slot);
      };

      auto jobs_active_end = std::ranges::begin(jobs);
      for (auto& slot : jobs) {
        if (refill_one(slot)) {
          ++jobs_active_end;
        } else {
          break;
        }
      }

      while (jobs_active_end != std::ranges::begin(jobs)) {
        auto it = std::ranges::begin(jobs);
        while (it != jobs_active_end) {
          auto& slot    = *it;
          auto  outcome = context.step(*slot.get());
          if (outcome.has_value()) {
            if (auto addresses = outcome.value()) {
              prefetch(addresses);
              ++it;
            } else {
              // Finalize current job
              auto fin = context.finalize(*slot.get());
              if (fin.has_value()) {
                if (auto& opt = fin.value(); opt.has_value()) {
                  std::invoke(reporter, completed, std::move(*slot.get()), std::move(*opt));
                } else {
                  std::invoke(reporter, terminated, std::move(*slot.get()));
                }
              } else {
                std::invoke(reporter, failed, std::move(*slot.get()), std::move(fin.error()));
              }

              // CRITICAL: Destroy the job before potentially overwriting it via refill or move
              std::destroy_at(slot.get());

              if (refill_one(slot)) {
                ++it;
              } else {
                // No more needles; compact the range
                if (std::next(it) != jobs_active_end) {
                  // Move the last active job into this slot
                  std::construct_at(slot.get(), std::move(*(std::prev(jobs_active_end)->get())));
                  std::destroy_at(std::prev(jobs_active_end)->get());
                }
                --jobs_active_end;
              }
            }
          } else {
            std::invoke(reporter, failed, std::move(*slot.get()), std::move(outcome.error()));
            std::destroy_at(slot.get()); // Destroy failed job

            if (refill_one(slot)) {
              ++it;
            } else {
              if (std::next(it) != jobs_active_end) {
                std::construct_at(slot.get(), std::move(*(std::prev(jobs_active_end)->get())));
                std::destroy_at(std::prev(jobs_active_end)->get());
              }
              --jobs_active_end;
            }
          }
        }
      }
    }
  };

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
