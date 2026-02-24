// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_DYNAMIC_HPP
#define VAULT_AMAC_DYNAMIC_HPP

#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <vault/algorithm/amac.hpp>

namespace vault::amac::concepts {

  /// @brief Defines a context capable of dynamic job generation.
  ///
  /// The dynamic context requires that `init`, `step`, and `finalize` accept an opaque `Sink`
  /// callable. This allows the state machine to emit new jobs (e.g., discovered child pointers
  /// in a DAG) without the executor needing to know the underlying storage mechanism.
  template <typename C, typename J, typename S>
  concept dynamic_job_context =
    std::move_constructible<J> && C::fanout() > 0uz && requires(C& context, J& job, S& sink) {
      { context.init(job, sink) } noexcept -> step_outcome;
      { context.step(job, sink) } noexcept -> step_outcome;
      { context.finalize(job, sink) } noexcept -> finalize_outcome;
    };

} // namespace vault::amac::concepts

namespace vault::amac {

  /**
   * @brief Single-Stage AMAC Executor with Dynamic Job Queuing.
   * @ingroup vault_amac
   * * Orchestrates the execution of asynchronous state machines that can dynamically spawn
   * new jobs during their execution (e.g., pointer chasing through a Directed Acyclic Graph).
   * * **Architecture:**
   * This executor is decoupled from the job storage. It relies on an opaque `Source` (to pull
   * jobs) and an opaque `Sink` (to push jobs). For optimal performance and to strictly bound
   * memory consumption during DAG traversals, the caller should back the Source and Sink
   * with a LIFO container (like `std::vector` or `std::deque`), which organically enforces a
   * cache-friendly Depth-First Search (DFS).
   * * @tparam TotalFanout The total number of parallel hardware prefetch slots.
   */
  template <uint8_t TotalFanout = 16>
  class dynamic_executor_fn {

    template <concepts::step_result J>
    static constexpr void prefetch(J const& step_result) {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(step_result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<J>>{});
    }

    /// Storage slot for in-flight jobs, using explicit lifecycles to protect the CFG.
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

      /// Encapsulated compaction to maintain straight-line loops for the optimizer.
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
    template <typename Context, typename Reporter, typename Source, typename Sink>
    static constexpr void operator()(Context&& ctx, Reporter&& reporter, Source&& source, Sink&& sink) {

      // Deduce the job type dynamically from the Source's return signature.
      using opt_job_t = std::remove_cvref_t<std::invoke_result_t<Source&>>;
      using job_t     = typename opt_job_t::value_type;

      static_assert(
        concepts::dynamic_job_context<std::remove_cvref_t<Context>, job_t, std::remove_cvref_t<Sink>>,
        "Context must satisfy dynamic_job_context (accepting the Sink in init/step/finalize)."
      );

      static constexpr auto const JOB_COUNT = (TotalFanout + ctx.fanout() - 1) / ctx.fanout();
      auto                        jobs      = std::array<job_slot<job_t>, JOB_COUNT>{};

      /// **Refill Helper**
      /// Pulls from the opaque `source`. Passes the `sink` to the context so it can emit children.
      auto refill_one = [&](this auto& self, auto& slot) -> bool {
        auto opt_job = std::invoke(source);
        if (!opt_job.has_value()) {
          return false;
        }

        auto&& job     = std::move(*opt_job);
        auto   outcome = ctx.init(job, sink);

        if (outcome.has_value()) {
          if (auto addresses = outcome.value()) {
            prefetch(addresses);
            std::construct_at(slot.get(), std::move(job));
            return true;
          }
          auto fin = ctx.finalize(job, sink);
          if (fin.has_value()) {
            if (auto& opt = fin.value(); opt.has_value()) {
              std::invoke(reporter, completed, std::move(job), std::move(*opt));
            } else {
              std::invoke(reporter, terminated, std::move(job));
            }
          } else {
            std::invoke(reporter, failed, std::move(job), std::move(fin.error()));
          }
          return self(slot); // Recurse if job completed synchronously
        }
        std::invoke(reporter, failed, std::move(job), std::move(outcome.error()));
        return self(slot);
      };

      // **Phase 1: Bootstrap**
      auto jobs_active_end = std::ranges::begin(jobs);
      for (auto& slot : jobs) {
        if (refill_one(slot)) {
          ++jobs_active_end;
        } else {
          break;
        }
      }

      // **Phase 2: Hot Loop Coordination**
      // Executes as long as there is at least one active job being processed.
      while (jobs_active_end != std::ranges::begin(jobs)) {
        auto it = std::ranges::begin(jobs);

        // Step all active jobs currently resident in the AMAC slots
        while (it != jobs_active_end) {
          auto& slot    = *it;
          auto  outcome = ctx.step(*slot.get(), sink);

          if (outcome.has_value()) {
            if (auto addresses = outcome.value()) {
              prefetch(addresses);
              ++it;
            } else {
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

              if (refill_one(slot)) {
                ++it;
              } else {
                it->compact_from(*std::prev(jobs_active_end));
                --jobs_active_end;
              }
            }
          } else {
            std::invoke(reporter, failed, std::move(*slot.get()), std::move(outcome.error()));
            std::destroy_at(slot.get());

            if (refill_one(slot)) {
              ++it;
            } else {
              it->compact_from(*std::prev(jobs_active_end));
              --jobs_active_end;
            }
          }
        }

        // **Phase 2b: Top-up**
        // If a finishing job generated multiple children into the Sink, the active window
        // might be < JOB_COUNT. Pull those newly generated children immediately into the pipeline.
        while (jobs_active_end != std::ranges::end(jobs)) {
          if (refill_one(*jobs_active_end)) {
            ++jobs_active_end;
          } else {
            break;
          }
        }
      }

      // **Phase 3: Cleanup** (Safety backstop, though technically empty here)
      for (auto it = std::ranges::begin(jobs); it != jobs_active_end; ++it) {
        std::destroy_at(it->get());
      }
    }
  };

  template <uint8_t TotalFanout = 16>
  constexpr inline auto const dynamic_executor = dynamic_executor_fn<TotalFanout>{};

} // namespace vault::amac

#endif
