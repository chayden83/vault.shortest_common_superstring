// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_CHUNKED_PIPELINE_HPP
#define VAULT_AMAC_CHUNKED_PIPELINE_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include <vault/algorithm/amac_pipeline.hpp>

namespace vault::amac {

  /**
   * @brief Functional executor for running a composed AMAC pipeline in cache-sized chunks.
   * @ingroup vault_amac
   *
   * This executor runs Stage A and Stage B sequentially, but chunks the input
   * range to ensure the intermediate jobs produced by Stage A never exceed the
   * physical capacity of the CPU's L1/L2 cache. This maximizes instruction
   * throughput while eliminating main memory (DRAM) bandwidth bottlenecks.
   *
   * @tparam MaxIntermediateBytes The maximum size in bytes of the intermediate buffer.
   * Defaults to 256 KB (a typical per-core L2 cache size).
   */
  template <std::size_t MaxIntermediateBytes = 262144>
  class chunked_pipeline_executor_fn {
  public:
    /**
     * @brief Executes a composed pipeline by chunking the input range.
     *
     * @param ijobs The input range of jobs for Stage A.
     * @param ctx The composed_context containing both contexts and the transition function.
     * @param reporter A callable invoked with completed or dropped jobs.
     */
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

      // Calculate exactly how many Stage A jobs we can process before
      // the intermediate Stage B jobs overflow the requested cache byte limit.
      constexpr auto chunk_size = std::max<std::size_t>(1uz, MaxIntermediateBytes / sizeof(job_b_t));

      // Allocate the intermediate buffer exactly once.
      auto intermediate_buffer = std::vector<job_b_t>{};
      intermediate_buffer.reserve(chunk_size);

      auto reporter_a = [&]<typename J>(J&& job) {
        if (auto opt_b = std::invoke(ctx.transition, ctx.ctx_a, ctx.ctx_b, job)) {
          intermediate_buffer.push_back(std::move(*opt_b));
        } else {
          std::invoke(reporter, std::move(job));
        }
      };

      // Lazily slice the input range into L2-cache-friendly blocks
      for (auto&& chunk : std::forward<Jobs>(ijobs) | std::views::chunk(chunk_size)) {
        intermediate_buffer.clear(); // Resets size to 0, retains capacity

        // Execute Stage A on the chunk. Transitions are pushed into the hot L2 buffer.
        vault::amac::executor<pure_ctx_t::fanout_a>(chunk, ctx.ctx_a, reporter_a);

        // Execute Stage B natively out of the L2 buffer.
        vault::amac::executor<pure_ctx_t::fanout_b>(intermediate_buffer, ctx.ctx_b, reporter);
      }
    }
  };

  /**
   * @brief Global instance of the chunked pipeline executor.
   * @ingroup vault_amac
   *
   * Usage: `vault::amac::chunked_pipeline_executor<256 * 1024>(haystack, composed_ctx, reporter);`
   */
  template <std::size_t MaxIntermediateBytes = 262144>
  constexpr inline auto const chunked_pipeline_executor = chunked_pipeline_executor_fn<MaxIntermediateBytes>{};

} // namespace vault::amac

#endif // VAULT_AMAC_CHUNKED_PIPELINE_HPP
