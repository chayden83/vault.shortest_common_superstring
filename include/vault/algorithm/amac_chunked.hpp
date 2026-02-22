// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_CHUNKED_PIPELINE_HPP
#define VAULT_AMAC_CHUNKED_PIPELINE_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include <vault/algorithm/amac_pipeline.hpp>

namespace vault::amac {

  namespace concepts {
    /**
     * @brief Concept to detect if a context is a composed AMAC pipeline.
     */
    template <typename T>
    concept composed_pipeline = requires {
      typename T::context_a_type;
      typename T::context_b_type;
      typename T::transition_fn_type;
    };
  } // namespace concepts

  /**
   * @brief A compile-time wrapper representing the transition edge between two pipeline stages.
   * * @tparam TransitionFn The invocable type representing the transition logic.
   * @tparam StepRatio The expected ratio of steps between the upstream and downstream stages.
   * @tparam TransProb The probability of a successful transition.
   */
  template <typename TransitionFn, typename StepRatio, typename TransProb>
  struct pipeline_edge {
    TransitionFn transition;
    using step_ratio             = StepRatio;
    using transition_probability = TransProb;
  };

  /**
   * @brief Creates a pipeline edge coupling a transition function with its performance policies.
   * * @tparam StepRatio A std::ratio representing the step ratio (B / A).
   * @tparam TransProb A std::ratio representing the probability of a successful transition.
   * @param transition The invocable object defining the transition logic.
   * @return A constructed pipeline_edge.
   */
  template <typename StepRatio, typename TransProb, typename TransitionFn>
  [[nodiscard]] constexpr auto make_edge(TransitionFn&& transition) {
    return pipeline_edge<std::remove_cvref_t<TransitionFn>, StepRatio, TransProb>{std::forward<TransitionFn>(transition
    )};
  }

  /**
   * @brief Base case for constructing a composed pipeline from two contexts and an edge.
   * * Ensures the resulting tree is strictly right-leaning by prohibiting Stage A
   * from being a composed pipeline itself.
   */
  template <typename CtxA, typename Edge, typename CtxB>
    requires(!concepts::composed_pipeline<std::remove_cvref_t<CtxA>>)
  [[nodiscard]] constexpr auto make_pipeline(CtxA&& ctx_a, Edge&& edge, CtxB&& ctx_b) {
    using pure_a    = std::remove_cvref_t<CtxA>;
    using pure_b    = std::remove_cvref_t<CtxB>;
    using pure_edge = std::remove_cvref_t<Edge>;

    return composed_context<
      pure_a,
      pure_b,
      decltype(std::declval<pure_edge>().transition),
      pure_a::fanout(),
      pure_b::fanout(),
      typename pure_edge::step_ratio,
      typename pure_edge::transition_probability>{
      .ctx_a      = std::forward<CtxA>(ctx_a),
      .ctx_b      = std::forward<CtxB>(ctx_b),
      .transition = std::forward<Edge>(edge).transition
    };
  }

  /**
   * @brief Recursive variadic factory for constructing N-stage right-leaning AMAC pipelines.
   * * This function folds a sequence of (Context, Edge, Context...) arguments from right
   * to left, generating a deeply nested composed_context that is perfectly optimized
   * for the chunked pipeline executor.
   */
  template <typename CtxA, typename Edge, typename CtxB, typename Edge2, typename... Rest>
  [[nodiscard]] constexpr auto make_pipeline(CtxA&& ctx_a, Edge&& edge, CtxB&& ctx_b, Edge2&& edge2, Rest&&... rest) {
    // Recursively fold the right side of the tree first
    auto right_tree = make_pipeline(std::forward<CtxB>(ctx_b), std::forward<Edge2>(edge2), std::forward<Rest>(rest)...);

    // Bind the left-most node to the folded right tree
    return make_pipeline(std::forward<CtxA>(ctx_a), std::forward<Edge>(edge), std::move(right_tree));
  }

  /**
   * @brief Functional executor for running a composed AMAC pipeline in cache-sized chunks.
   * @ingroup vault_amac
   *
   * This executor runs Stage A and Stage B sequentially, but chunks the input
   * range to ensure the intermediate jobs produced by Stage A never exceed the
   * physical capacity of the CPU's L1/L2 cache. This maximizes instruction
   * throughput while eliminating main memory (DRAM) bandwidth bottlenecks.
   *
   * @tparam fanoutBudget The total number of parallel hardware prefetch slots.
   * @tparam maxIntermediateBytes The maximum size in bytes of the intermediate buffer.
   * Defaults to 256 KB (a typical per-core L2 cache size).
   */
  template <std::size_t fanoutBudget = 16, std::size_t maxIntermediateBytes = 262144>
  class chunked_pipeline_executor_fn {
  public:
    template <std::ranges::input_range Jobs, typename ComposedCtx, typename Reporter>
    static constexpr void operator()(Jobs&& ijobs, ComposedCtx&& ctx, Reporter&& reporter) {
      using pure_ctx_t = std::remove_cvref_t<ComposedCtx>;

      static_assert(
        !concepts::composed_pipeline<typename pure_ctx_t::context_a_type>,
        "AMAC pipelines must form right-leaning trees: A -> (B -> C). "
        "Stage A cannot be a composed pipeline."
      );

      using job_a_t = std::ranges::range_value_t<Jobs>;
      using opt_b_t = std::invoke_result_t<
        typename pure_ctx_t::transition_fn_type&,
        typename pure_ctx_t::context_a_type&,
        typename pure_ctx_t::context_b_type&,
        job_a_t&>;
      using job_b_t = typename opt_b_t::value_type;

      // 1. Calculate the max capacity of the L2 cache for Stage B jobs
      constexpr auto max_b_capacity = std::max<std::size_t>(1uz, maxIntermediateBytes / sizeof(job_b_t));

      // 2. Scale the input chunk size by the inverse transition probability
      constexpr double inverse_prob = static_cast<double>(pure_ctx_t::transition_probability::den) /
                                      static_cast<double>(pure_ctx_t::transition_probability::num);

      constexpr auto input_chunk_size =
        std::max<std::size_t>(1uz, static_cast<std::size_t>(static_cast<double>(max_b_capacity) * inverse_prob));

      // Small padding for statistical variance
      auto extra_capacity = std::max(100uz, static_cast<std::size_t>(max_b_capacity * 1.25));

      auto intermediate_buffer = std::vector<job_b_t>{};
      // Reserve based on the expected output, preventing reallocations
      intermediate_buffer.reserve(std::min(input_chunk_size, max_b_capacity + extra_capacity));

      auto reporter_a = [&]<typename J>(J&& job) {
        if (auto opt_b = std::invoke(ctx.transition, ctx.ctx_a, ctx.ctx_b, job)) {
          intermediate_buffer.push_back(std::move(*opt_b));
        } else {
          std::invoke(reporter, std::move(job));
        }
      };

      for (auto&& chunk : std::forward<Jobs>(ijobs) | std::views::chunk(input_chunk_size)) {
        intermediate_buffer.clear();

        // Execute sequentially using the FULL fanout budget for Stage A
        vault::amac::executor<fanoutBudget>(chunk, ctx.ctx_a, reporter_a);

        // Recursively evaluate Stage B if it's a nested pipeline, otherwise run it natively.
        if constexpr (concepts::composed_pipeline<typename pure_ctx_t::context_b_type>) {
          vault::amac::chunked_pipeline_executor_fn<fanoutBudget, maxIntermediateBytes>{}(
            intermediate_buffer, ctx.ctx_b, reporter
          );
        } else {
          vault::amac::executor<fanoutBudget>(intermediate_buffer, ctx.ctx_b, reporter);
        }
      }
    }
  };

  /**
   * @brief Global instance of the chunked pipeline executor.
   * @ingroup vault_amac
   *
   * Usage: `vault::amac::chunked_pipeline_executor<16, 256 * 1024>(haystack, composed_ctx, reporter);`
   */
  template <std::size_t fanoutBudget = 16, std::size_t maxIntermediateBytes = 262144>
  constexpr inline auto const chunked_pipeline_executor =
    chunked_pipeline_executor_fn<fanoutBudget, maxIntermediateBytes>{};

} // namespace vault::amac

#endif // VAULT_AMAC_CHUNKED_PIPELINE_HPP
