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
  /// Tag used to signal to the reporter that a job completed successfully and yielded a payload.
  constexpr inline struct completed_tag {
  } const completed;

  /// Tag used to signal to the reporter that a job terminated without yielding a payload or error.
  constexpr inline struct terminated_tag {
  } const terminated;

  /// Tag used to signal to the reporter that a job failed and yielded an error.
  constexpr inline struct failed_tag {
  } const failed;
} // namespace vault::amac

namespace vault::amac::concepts {
  /// Defines a valid step result.
  /// A step result must be convertible to `bool` (indicating if memory addresses are available)
  /// and must act as a tuple where every element is a `void const*` used for prefetching.
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

  /// Defines the interface required for a state machine managing an asynchronous job.
  ///
  /// The context must provide `init`, `step`, and `finalize` methods, and expose a static
  /// `fanout()` indicating its contribution to the pipeline's concurrency budget.
  template <typename C, typename J>
  concept job_context = std::move_constructible<J> && C::fanout() > 0uz && requires(C& context, J& job) {
    { context.init(job) } noexcept -> step_outcome;
    { context.step(job) } noexcept -> step_outcome;
    { context.finalize(job) } noexcept -> finalize_outcome;
  };

  /// Defines the interface for a callback mechanism that routes job results.
  template <typename R, typename J, typename P, typename E>
  concept job_reporter = std::invocable<R, completed_tag, J&&, P&&> && std::invocable<R, terminated_tag, J&&> &&
                         std::invocable<R, failed_tag, J&&, E&&>;
} // namespace vault::amac::concepts

namespace vault::amac {
  /// Standard container for prefetch addresses yielded by `step()`.
  template <std::size_t N>
  struct step_result : public std::array<void const*, N> {
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
      return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((*this)[Is] != nullptr) || ...);
      }(std::make_index_sequence<N>{});
    }
  };

  /// **Single-Stage AMAC Executor**
  ///
  /// Orchestrates the execution of multiple independent state machines, interleaving their
  /// execution to hide memory latency. Uses `__builtin_prefetch` on addresses provided by the context.
  template <uint8_t TotalFanout = 16>
  class executor_fn {

    /// Issues hardware prefetch instructions for all addresses yielded by the step result.
    template <concepts::step_result J>
    static constexpr void prefetch(J const& step_result) {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(step_result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<J>>{});
    }

    /// **Storage Slot for In-Flight Jobs**
    ///
    /// Maintains raw byte storage for jobs to allow strict, manual lifecycle management.
    ///
    /// **Design Rationale**:
    /// We use an uninitialized byte array rather than `std::optional` or direct objects to avoid
    /// implicit destructor calls or branching flags. This guarantees that we control exactly when
    /// `std::destroy_at` and `std::construct_at` are invoked, satisfying both the LeakSanitizer
    /// and the C++ object model without inserting optimization barriers into the hot loop.
    template <typename J>
    class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      [[nodiscard]] job_slot() = default;

      /// **Standard Move Assignment**
      /// Overwrites the current live object with the contents of another live object.
      job_slot& operator=(job_slot&& other) {
        if (this != std::addressof(other)) {
          *this->get() = std::move(*other.get());
        }
        return *this;
      }

      /// **Compaction Logic (CRITICAL PERFORMANCE BOUNDARY)**
      ///
      /// Moves the contents of `other` into this slot, and immediately destroys `other`.
      ///
      /// **Why this is encapsulated:**
      /// If this logic (and its associated bounds checking) is written directly into the main
      /// `executor_fn::operator()` loop, the compiler's loop unroller interprets the extra basic
      /// blocks as a highly complex Control Flow Graph (CFG). This causes Clang to abandon
      /// vectorization and register-pinning, resulting in a ~20% throughput regression.
      /// By hiding the branch and the placement new/delete behind this member function, the
      /// optimizer is able to lower this to straight-line code (often a CMOV) on the hot path.
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
    /// Executes the AMAC pipeline over a range of jobs.
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

      // The array of slots representing our concurrent execution window.
      auto jobs = std::array<job_slot<job_t>, JOB_COUNT>{};

      /// **Refill Helper**
      /// Attempts to pull a new job from the input sequence, initialize it, and place it
      /// into the provided slot. If the job completes immediately during `init()`, it recurses.
      auto refill_one = [&](this auto& self, auto& slot) -> bool {
        if (ijobs_cursor == ijobs_last) {
          return false;
        }
        auto&& job     = *ijobs_cursor++;
        auto   outcome = context.init(job);

        if (outcome.has_value()) {
          if (auto addresses = outcome.value()) {
            // Asynchronous state: memory fetch required. Place in active window.
            prefetch(addresses);
            std::construct_at(slot.get(), std::forward<decltype(job)>(job));
            return true;
          }
          // Synchronous state: job completed during init without needing a fetch.
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
          // Recurse to find a job that actually needs fetching.
          return self(slot);
        }

        // Initialization failed.
        std::invoke(reporter, failed, std::forward<decltype(job)>(job), std::move(outcome.error()));
        return self(slot);
      };

      // **Phase 1: Bootstrap the Pipeline**
      // Fill the job array up to the concurrency limit (or until input is exhausted).
      auto jobs_active_end = std::ranges::begin(jobs);
      for (auto& slot : jobs) {
        if (refill_one(slot)) {
          ++jobs_active_end;
        } else {
          break;
        }
      }

      // **Phase 2: The Hot Path (Coordination Loop)**
      // Iterates continuously over the contiguous active window `[jobs.begin(), jobs_active_end)`.
      // NOTE: This loop must remain structurally simple to guarantee aggressive unrolling.
      while (jobs_active_end != std::ranges::begin(jobs) || ijobs_cursor != ijobs_last) {
        auto it = std::ranges::begin(jobs);
        while (it != jobs_active_end) {
          auto& slot    = *it;
          auto  outcome = context.step(*slot.get());

          if (outcome.has_value()) {
            if (auto addresses = outcome.value()) {
              // The Happy Path: Job needs data, issue prefetch, move to next slot.
              prefetch(addresses);
              ++it;
            } else {
              // Job completed successfully.
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

              // Free the current object to satisfy C++ lifetime rules.
              std::destroy_at(slot.get());

              // Attempt to refill the now-empty slot.
              if (refill_one(slot)) {
                ++it;
              } else {
                // Input is empty. Compact the range by moving the last active element into
                // this slot. This maintains the contiguous block required by the hot path.
                it->compact_from(*std::prev(jobs_active_end));
                --jobs_active_end;
              }
            }
          } else {
            // Job failed during step.
            std::invoke(reporter, failed, std::move(*slot.get()), std::move(outcome.error()));

            // Free the current object to satisfy C++ lifetime rules.
            std::destroy_at(slot.get());

            // Attempt to refill or compact, identical to the completion path above.
            if (refill_one(slot)) {
              ++it;
            } else {
              it->compact_from(*std::prev(jobs_active_end));
              --jobs_active_end;
            }
          }
        }
      }

      // **Phase 3: Final Cleanup**
      // Destroy any remaining jobs that were active when the pipeline drained.
      for (auto it = std::ranges::begin(jobs); it != jobs_active_end; ++it) {
        std::destroy_at(it->get());
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
