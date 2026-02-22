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
  template <std::size_t FanoutBudget = 16, std::size_t MaxIntermediateBytes = 262144>
  class chunked_pipeline_executor_fn {
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

      // 1. Calculate the max capacity of the L2 cache for Stage B jobs
      constexpr auto max_b_capacity = std::max<std::size_t>(1uz, MaxIntermediateBytes / sizeof(job_b_t));

      // 2. Scale the input chunk size by the inverse transition probability
      constexpr double inverse_prob = static_cast<double>(pure_ctx_t::transition_probability::den) /
                                      static_cast<double>(pure_ctx_t::transition_probability::num);

      constexpr auto input_chunk_size =
        std::max<std::size_t>(1uz, static_cast<std::size_t>(static_cast<double>(max_b_capacity) * inverse_prob));

      auto intermediate_buffer = std::vector<job_b_t>{};
      // Reserve based on the expected output, preventing reallocations
      intermediate_buffer.reserve(max_b_capacity + 100); // Small padding for statistical variance

      auto reporter_a = [&]<typename J>(J&& job) {
        if (auto opt_b = std::invoke(ctx.transition, ctx.ctx_a, ctx.ctx_b, job)) {
          intermediate_buffer.push_back(std::move(*opt_b));
        } else {
          std::invoke(reporter, std::move(job));
        }
      };

      for (auto&& chunk : std::forward<Jobs>(ijobs) | std::views::chunk(input_chunk_size)) {
        intermediate_buffer.clear();

        // Execute sequentially using the FULL fanout budget for each stage
        vault::amac::executor<FanoutBudget>(chunk, ctx.ctx_a, reporter_a);
        vault::amac::executor<FanoutBudget>(intermediate_buffer, ctx.ctx_b, reporter);
      }
    }
  };

  /**
   * @brief Global instance of the chunked pipeline executor.
   * @ingroup vault_amac
   *
   * Usage: `vault::amac::chunked_pipeline_executor<256 * 1024>(haystack, composed_ctx, reporter);`
   */
  template <std::size_t FanoutBudget = 16, std::size_t MaxIntermediateBytes = 262144>
  constexpr inline auto const chunked_pipeline_executor =
    chunked_pipeline_executor_fn<FanoutBudget, MaxIntermediateBytes>{};

} // namespace vault::amac

#endif // VAULT_AMAC_CHUNKED_PIPELINE_HPP
