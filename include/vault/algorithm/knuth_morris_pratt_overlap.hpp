// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_OVERLAP_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_OVERLAP_HPP

#include <algorithm>
#include <concepts>
#include <functional>
#include <iterator>
#include <ranges>
#include <vector>

#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>

namespace vault::algorithm {

  /**
   * @brief Function object for computing the overlap between a text (LHS) and a
   * pattern (RHS) using the Knuth-Morris-Pratt algorithm.
   *
   * This algorithm finds the longest suffix of the text (LHS) that matches a
   * prefix of the pattern (RHS). It is useful for finding how much two strings
   * overlap when concatenating them (e.g., in DNA assembly or shortest common
   * superstring problems).
   *
   * @example
   * ```cpp
   * #include <string_view>
   * #include <iostream>
   * #include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
   *
   * int main() {
   * using namespace std::literals;
   * auto lhs = "pine"sv;
   * auto rhs = "apple"sv;
   *
   * // Standard overlap
   * auto res = vault::algorithm::knuth_morris_pratt_overlap(lhs, rhs);
   * // res.score == 1 (The 'e' at end of pine matches 'e' at start of apple?
   * No.)
   * // Wait, "pine" and "apple". "e" != "a". Overlap is 0.
   * auto text = "ababa"sv;
   * auto pattern = "ababa"sv;
   * auto res2 = vault::algorithm::knuth_morris_pratt_overlap(text, pattern);
   * // res2.score == 3 ("aba" suffix matches "aba" prefix)
   * // Using projections (case insensitive comparison example)
   * auto res3 = vault::algorithm::knuth_morris_pratt_overlap(
   * "FOOBAR"sv, "barstool"sv,
   * {}, // default failure table (computed internally)
   * std::equal_to<>{},
   * [](char c) { return std::tolower(c); },
   * [](char c) { return std::tolower(c); }
   * );
   * // res3.score == 3 ("BAR" matches "bar")
   * }
   * ```
   */
  constexpr inline struct knuth_morris_pratt_overlap_fn {

    /**
     * @brief Result structure containing the overlap score and relevant
     * iterators.
     *
     * @tparam ILHS Iterator type for the Left Hand Side (Text).
     * @tparam IRHS Iterator type for the Right Hand Side (Pattern).
     * @tparam FailureTable Type of the KMP failure table (defaults to
     * std::vector<int>).
     */
    template <std::forward_iterator    ILHS,
      std::forward_iterator            IRHS,
      knuth_morris_pratt_failure_table FailureTable = std::vector<int>>
    struct result {
      /// The length of the longest overlap found.
      std::iter_difference_t<IRHS> score;

      /// Iterator to the start of the overlap in LHS.
      ILHS lhs_first;
      /// Iterator to the end of LHS.
      ILHS lhs_last;

      /// Iterator to the start of RHS.
      IRHS rhs_first;
      /// Iterator to the end of the matching prefix in RHS.
      IRHS rhs_last;

      /// The failure table used during the computation.
      FailureTable failure_table;
    };

    /**
     * @brief Computes the overlap between two ranges using a pre-computed
     * failure table.
     *
     * @tparam ILHS Input iterator type for the Text (LHS).
     * @tparam SLHS Sentinel type for the Text (LHS).
     * @tparam IRHS Input iterator type for the Pattern (RHS).
     * @tparam SRHS Sentinel type for the Pattern (RHS).
     * @tparam FailureTable Type of the pre-computed failure table.
     * @tparam Comp Binary predicate type. Defaults to `std::equal_to<>`.
     * @tparam ProjLHS Projection type for LHS elements. Defaults to
     * `std::identity`.
     * @tparam ProjRHS Projection type for RHS elements. Defaults to
     * `std::identity`.
     *
     * @param lhs_first Iterator to the beginning of the text.
     * @param lhs_last Sentinel for the end of the text.
     * @param rhs_first Iterator to the beginning of the pattern.
     * @param rhs_last Sentinel for the end of the pattern.
     * @param failure_table A valid KMP failure table computed for the pattern
     * (RHS).
     * @param comp Comparison function object.
     * @param proj_lhs Projection to apply to LHS elements before comparison.
     * @param proj_rhs Projection to apply to RHS elements before comparison.
     *
     * @return result structure containing the overlap score and iterators.
     *
     * @par Complexity
     * @parblock
     * - **Time:** O(N), where N is `std::distance(lhs_first, lhs_last)`. The
     * algorithm performs at most 2N comparisons.
     * - **Space:** O(1) auxiliary space (excluding the storage for the provided
     * failure table).
     * @endparblock
     */
    template <std::forward_iterator    ILHS,
      std::sentinel_for<ILHS>          SLHS,
      std::forward_iterator            IRHS,
      std::sentinel_for<IRHS>          SRHS,
      knuth_morris_pratt_failure_table FailureTable,
      typename Comp    = std::equal_to<>,
      typename ProjLHS = std::identity,
      typename ProjRHS = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<ILHS, ProjLHS>,
        std::projected<IRHS, ProjRHS>>
    [[nodiscard]] static constexpr auto operator()(ILHS lhs_first,
      SLHS                                              lhs_last,
      IRHS                                              rhs_first,
      SRHS                                              rhs_last,
      FailureTable&&                                    failure_table,
      Comp                                              comp     = {},
      ProjLHS                                           proj_lhs = {},
      ProjRHS proj_rhs = {}) -> result<ILHS, IRHS, FailureTable>
    {
      auto rhs_index = std::iter_difference_t<IRHS>{0};

      // Note: std::ranges::distance may be O(N) for non-random-access
      // iterators. This is necessary to construct the return iterators
      // correctly.
      auto lhs_length = std::ranges::distance(lhs_first, lhs_last);
      auto rhs_length = std::ranges::distance(rhs_first, rhs_last);

      auto lhs_cursor = lhs_first;

      // Iterating through the text (LHS)
      for (; lhs_cursor != lhs_last; ++lhs_cursor) {

        // Helper to perform the comparison with projections
        auto check_match = [&](auto const& iter_rhs) {
          return std::invoke(comp,
            std::invoke(proj_lhs, *lhs_cursor),
            std::invoke(proj_rhs, *iter_rhs));
        };

        // KMP State Machine Transition
        while (rhs_index > 0) {
          // If characters match, we stop backtracking
          // Safety check: rhs_index != rhs_length prevents dereferencing end
          // iterator
          if (rhs_index != rhs_length
            && check_match(std::next(rhs_first, rhs_index))) {
            break;
          }
          // Backtrack using the failure table
          rhs_index = failure_table[rhs_index - 1];
        }

        // Check for match at current position
        if (rhs_index != rhs_length
          && check_match(std::next(rhs_first, rhs_index))) {
          rhs_index++;
        }
      }

      // Reconstruct iterators for the result
      return result<ILHS, IRHS, FailureTable>{rhs_index,
        // Iterator to where the overlap begins in LHS
        std::next(lhs_first, lhs_length - rhs_index),
        lhs_cursor, // Iterator to the end of LHS
        rhs_first,
        // Iterator to the end of the matched prefix in RHS
        std::next(rhs_first, rhs_index),
        std::forward<FailureTable>(failure_table)};
    }

    /**
     * @brief Computes the overlap by calculating the failure table on the fly.
     *
     * This overload computes the failure table internally before running the
     * overlap algorithm.
     *
     * @tparam ILHS Input iterator type for the Text (LHS).
     * @tparam SLHS Sentinel type for the Text (LHS).
     * @tparam IRHS Input iterator type for the Pattern (RHS).
     * @tparam SRHS Sentinel type for the Pattern (RHS).
     * @tparam Comp Binary predicate type.
     * @tparam ProjLHS Projection type for LHS elements.
     * @tparam ProjRHS Projection type for RHS elements.
     *
     * @param lhs_first Iterator to the beginning of the text.
     * @param lhs_last Sentinel for the end of the text.
     * @param rhs_first Iterator to the beginning of the pattern.
     * @param rhs_last Sentinel for the end of the pattern.
     * @param comp Comparison function object.
     * @param proj_lhs Projection to apply to LHS elements.
     * @param proj_rhs Projection to apply to RHS elements.
     *
     * @return result structure containing the overlap score and iterators.
     *
     * @par Complexity
     * @parblock
     * - **Time:** O(N + M), where N is `lhs` length and M is `rhs` length (due
     * to failure table construction).
     * - **Space:** O(M) auxiliary space to store the failure table.
     * @endparblock
     */
    template <std::forward_iterator ILHS,
      std::sentinel_for<ILHS>       SLHS,
      std::forward_iterator         IRHS,
      std::sentinel_for<IRHS>       SRHS,
      typename Comp    = std::equal_to<>,
      typename ProjLHS = std::identity,
      typename ProjRHS = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<ILHS, ProjLHS>,
        std::projected<IRHS, ProjRHS>>
    [[nodiscard]] static constexpr auto operator()(ILHS lhs_first,
      SLHS                                              lhs_last,
      IRHS                                              rhs_first,
      SRHS                                              rhs_last,
      Comp                                              comp     = {},
      ProjLHS                                           proj_lhs = {},
      ProjRHS proj_rhs = {}) -> result<ILHS, IRHS>
    {
      // Compute failure table for RHS using the same comparator and RHS
      // projection. Note: Forwarding 'proj_rhs' is critical for correct
      // self-matching behavior.
      auto failure_table = knuth_morris_pratt_failure_function(
        rhs_first, rhs_last, comp, proj_rhs);

      return operator()(lhs_first,
        lhs_last,
        rhs_first,
        rhs_last,
        std::move(failure_table),
        comp,
        proj_lhs,
        proj_rhs);
    }

    /**
     * @brief Range overload with pre-computed failure table.
     *
     * @tparam LHS Range type for the Text.
     * @tparam RHS Range type for the Pattern.
     * @tparam FailureTable Type of the pre-computed failure table.
     * @tparam Comp Binary predicate type.
     * @tparam ProjLHS Projection type for LHS elements.
     * @tparam ProjRHS Projection type for RHS elements.
     *
     * @param lhs The text range.
     * @param rhs The pattern range.
     * @param failure_table A valid KMP failure table computed for the pattern
     * (RHS).
     * @param comp Comparison function object.
     * @param proj_lhs Projection to apply to LHS elements.
     * @param proj_rhs Projection to apply to RHS elements.
     *
     * @return result structure containing the overlap score and iterators.
     *
     * @par Complexity
     * @parblock
     * - **Time:** O(N), where N is the size of LHS.
     * - **Space:** O(1) auxiliary space.
     * @endparblock
     */
    template <std::ranges::forward_range LHS,
      std::ranges::forward_range         RHS,
      knuth_morris_pratt_failure_table   FailureTable,
      typename Comp    = std::equal_to<>,
      typename ProjLHS = std::identity,
      typename ProjRHS = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<std::ranges::iterator_t<LHS>, ProjLHS>,
        std::projected<std::ranges::iterator_t<RHS>, ProjRHS>>
    [[nodiscard]] static constexpr auto operator()(LHS&& lhs,
      RHS&&                                              rhs,
      FailureTable&&                                     failure_table,
      Comp                                               comp     = {},
      ProjLHS                                            proj_lhs = {},
      ProjRHS proj_rhs = {}) -> result<std::ranges::iterator_t<LHS>,
      std::ranges::iterator_t<RHS>,
      FailureTable>
    {
      return operator()(std::ranges::begin(lhs),
        std::ranges::end(lhs),
        std::ranges::begin(rhs),
        std::ranges::end(rhs),
        std::forward<FailureTable>(failure_table),
        comp,
        proj_lhs,
        proj_rhs);
    }

    /**
     * @brief Range overload without pre-computed failure table.
     *
     * This overload computes the failure table internally.
     *
     * @tparam LHS Range type for the Text.
     * @tparam RHS Range type for the Pattern.
     * @tparam Comp Binary predicate type.
     * @tparam ProjLHS Projection type for LHS elements.
     * @tparam ProjRHS Projection type for RHS elements.
     *
     * @param lhs The text range.
     * @param rhs The pattern range.
     * @param comp Comparison function object.
     * @param proj_lhs Projection to apply to LHS elements.
     * @param proj_rhs Projection to apply to RHS elements.
     *
     * @return result structure containing the overlap score and iterators.
     *
     * @par Complexity
     * @parblock
     * - **Time:** O(N + M), where N is `lhs` size and M is `rhs` size.
     * - **Space:** O(M) auxiliary space for failure table.
     * @endparblock
     */
    template <std::ranges::forward_range LHS,
      std::ranges::forward_range         RHS,
      typename Comp    = std::equal_to<>,
      typename ProjLHS = std::identity,
      typename ProjRHS = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<std::ranges::iterator_t<LHS>, ProjLHS>,
        std::projected<std::ranges::iterator_t<RHS>, ProjRHS>>
    [[nodiscard]] static constexpr auto operator()(LHS&& lhs,
      RHS&&                                              rhs,
      Comp                                               comp     = {},
      ProjLHS                                            proj_lhs = {},
      ProjRHS                                            proj_rhs = {})
      -> result<std::ranges::iterator_t<LHS>, std::ranges::iterator_t<RHS>>
    {
      return operator()(std::ranges::begin(lhs),
        std::ranges::end(lhs),
        std::ranges::begin(rhs),
        std::ranges::end(rhs),
        comp,
        proj_lhs,
        proj_rhs);
    }

  } const knuth_morris_pratt_overlap{};

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_OVERLAP_HPP
