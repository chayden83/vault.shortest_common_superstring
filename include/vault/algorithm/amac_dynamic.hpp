// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_DYNAMIC_HPP
#define VAULT_AMAC_DYNAMIC_HPP

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <vault/algorithm/amac.hpp>

namespace vault::amac::concepts {
  template <typename C, typename J, typename S>
  concept dynamic_job_context =
    std::move_constructible<J> && C::fanout() > 0uz && requires(C& context, J& job, S& sink) {
      { context.init(job, sink) } noexcept -> step_outcome;
      { context.step(job, sink) } noexcept -> step_outcome;
      { context.finalize(job, sink) } noexcept -> finalize_outcome;
    };
} // namespace vault::amac::concepts

namespace vault::amac {

  template <uint8_t TotalFanout = 16>
  class dynamic_executor_fn {
    template <concepts::step_result J>
    static constexpr void prefetch(J const& step_result) {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(step_result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<J>>{});
    }

    // Barebones storage: no compaction logic, no moves.
    template <typename J>
    class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      [[nodiscard]] J* get() noexcept {
        return reinterpret_cast<J*>(&storage[0]);
      }
    };

  public:
    template <typename Context, typename Reporter, typename Source, typename Sink>
    static constexpr void operator()(Context&& ctx, Reporter&& reporter, Source&& source, Sink&& sink) {
      using pure_ctx_t = std::remove_cvref_t<Context>;
      using opt_job_t  = std::remove_cvref_t<std::invoke_result_t<Source&>>;
      using job_t      = typename opt_job_t::value_type;

      static_assert(
        concepts::dynamic_job_context<pure_ctx_t, job_t, std::remove_cvref_t<Sink>>,
        "Context must satisfy dynamic_job_context."
      );

      static constexpr auto const JOB_COUNT = (TotalFanout + pure_ctx_t::fanout() - 1) / pure_ctx_t::fanout();
      static_assert(JOB_COUNT <= 64, "Dynamic executor bitmask currently limits JOB_COUNT to 64.");

      // Calculate the fully saturated bitmask (e.g., 0xFFFF for JOB_COUNT = 16)
      static constexpr uint64_t FULL_MASK = (JOB_COUNT == 64) ? ~0ULL : (1ULL << JOB_COUNT) - 1;

      auto     jobs        = std::array<job_slot<job_t>, JOB_COUNT>{};
      uint64_t active_mask = 0;

      // **Optimized Refill logic**
      // Flattened into a while(true) loop to eliminate recursive stack frames.
      auto refill = [&](std::size_t idx) -> bool {
        while (true) {
          auto opt_job = std::invoke(source);
          if (!opt_job.has_value()) {
            return false;
          }

          std::construct_at(jobs[idx].get(), std::move(*opt_job));
          auto outcome = ctx.init(*jobs[idx].get(), sink);

          if (outcome.has_value()) {
            if (auto addresses = outcome.value()) {
              prefetch(addresses);
              active_mask |= (1ULL << idx);
              return true;
            }
            // Synchronous completion (bypasses step)
            auto fin = ctx.finalize(*jobs[idx].get(), sink);
            if (fin.has_value()) {
              if (auto& opt = fin.value(); opt.has_value()) {
                std::invoke(reporter, completed, std::move(*jobs[idx].get()), std::move(*opt));
              } else {
                std::invoke(reporter, terminated, std::move(*jobs[idx].get()));
              }
            } else {
              std::invoke(reporter, failed, std::move(*jobs[idx].get()), std::move(fin.error()));
            }
            std::destroy_at(jobs[idx].get());
            continue; // Loop around and attempt to fill this same slot again
          }

          // Init Failed
          std::invoke(reporter, failed, std::move(*jobs[idx].get()), std::move(outcome.error()));
          std::destroy_at(jobs[idx].get());
          continue;
        }
      };

      // **Phase 1: Bootstrap**
      for (std::size_t i = 0; i < JOB_COUNT; ++i) {
        if (!refill(i)) {
          break;
        }
      }

      // **Phase 2: Hot Loop Coordination**
      while (active_mask != 0) {

        // 2a. Step all active jobs using bit-clearing iteration
        uint64_t current_mask = active_mask;
        while (current_mask != 0) {
          // Hardware-accelerated trailing zero count finds the next job instantly
          std::size_t idx = std::countr_zero(current_mask);
          // Hardware bit-reset clears the bit we just processed
          current_mask &= current_mask - 1;

          auto& slot    = jobs[idx];
          auto  outcome = ctx.step(*slot.get(), sink);

          if (outcome.has_value()) {
            if (auto addresses = outcome.value()) {
              prefetch(addresses);
            } else {
              // Job finished
              auto fin = ctx.finalize(*slot.get(), sink);
              if (fin.has_value()) {
                if (auto& opt = fin.value(); opt.has_value()) {
                  std::invoke(reporter, completed, std::move(*slot.get()), std::move(*opt));
                } else {
                  std::invoke(reporter, terminated, std::move(*slot.get()));
                }
              } else {
                std::invoke(reporter, failed, std::move(*slot.get()), std::move(fin.error()));
              }

              std::destroy_at(slot.get());
              active_mask &= ~(1ULL << idx); // Mark slot as dead
              refill(idx);                   // Attempt immediate replacement
            }
          } else {
            // Job failed
            std::invoke(reporter, failed, std::move(*slot.get()), std::move(outcome.error()));
            std::destroy_at(slot.get());
            active_mask &= ~(1ULL << idx);
            refill(idx);
          }
        }

        // 2b. Top-up sweep for any slots that couldn't be filled during the step phase
        // (e.g., if a job spawned multiple children, the sink now has items we can pull)
        uint64_t empty_mask = ~active_mask & FULL_MASK;
        while (empty_mask != 0) {
          std::size_t idx = std::countr_zero(empty_mask);
          if (!refill(idx)) {
            break; // Source is entirely empty, stop checking
          }
          empty_mask &= empty_mask - 1;
        }
      }
    }
  };

  template <uint8_t TotalFanout = 16>
  constexpr inline auto const dynamic_executor = dynamic_executor_fn<TotalFanout>{};

} // namespace vault::amac

#endif // VAULT_AMAC_DYNAMIC_HPP
