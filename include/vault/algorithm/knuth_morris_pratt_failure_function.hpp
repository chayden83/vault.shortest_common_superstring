// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_FAILURE_FUNCTION_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_FAILURE_FUNCTION_HPP

#include <algorithm>
#include <cassert>
#include <concepts>
#include <functional>
#include <iterator>
#include <ranges>
#include <vector>

namespace vault::algorithm {

  /**
   * @brief Concept ensuring a type is suitable for storing a KMP failure table.
   *
   * A failure table is typically a sequence of integers where the i-th element
   * represents the length of the longest proper prefix of the substring ending
   * at i that is also a suffix of that substring.
   *
   * @tparam T The container type.
   */
  template <typename T>
  concept knuth_morris_pratt_failure_table = true
    && std::copy_constructible<T> && std::ranges::random_access_range<T>
    && std::integral<std::ranges::range_value_t<T>>;

  /**
   * @brief Function object for computing the Knuth-Morris-Pratt (KMP) failure
   * function.
   *
   * The failure function (also known as the prefix function or pi-table) is a
   * key component of the KMP string matching algorithm. For a given pattern
   * `P`, the failure function `pi[i]` stores the length of the longest proper
   * prefix of `P[0...i]` that is also a suffix of `P[0...i]`.
   *
   * This precomputed table allows the search phase to skip comparisons after a
   * mismatch.
   */
  constexpr inline struct knuth_morris_pratt_failure_function_fn {

    /**
     * @brief Computes the KMP failure table for a range defined by a pair of
     * iterators.
     *
     * @tparam I Input iterator type. Must satisfy `std::forward_iterator`.
     * @tparam S Sentinel type for the iterator.
     * @tparam Comp Binary predicate type for comparison. Defaults to
     * `std::equal_to<>`.
     * @tparam Proj Projection type to apply to elements before comparison.
     * Defaults to `std::identity`.
     *
     * @param first Iterator to the beginning of the pattern.
     * @param last Sentinel for the end of the pattern.
     * @param comp Comparison predicate. Returns `true` if two elements are
     * equivalent.
     * @param proj Projection to apply to elements.
     *
     * @return `std::vector<int>` containing the failure table. The size of the
     * vector is equal to `std::distance(first, last)`.
     *
     * @complexity
     * O(N), where N is `std::distance(first, last)`.
     * Although the inner while-loop (implied by the state resets) might iterate
     * multiple times, the variable `length_of_previous_longest_prefix` is
     * incremented at most N times and decremented at most N times throughout
     * the entire execution.
     */
    template <std::forward_iterator I,
      std::sentinel_for<I>          S,
      typename Comp = std::equal_to<>,
      typename Proj = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<I, Proj>,
        std::projected<I, Proj>>
    [[nodiscard]] static constexpr auto operator()(
      I first, S last, Comp comp = {}, Proj proj = {}) -> std::vector<int>
    {
      auto length_of_pattern = std::ranges::distance(first, last);

      // The failure function is only defined for non-negative lengths.
      assert(length_of_pattern >= 0 && "Pattern length cannot be negative.");

      // Note: Parentheses are required here (instead of braces) to invoke the
      // size/value constructor rather than the initializer_list constructor.
      auto failure_function = std::vector<int>(length_of_pattern, 0);

      if (length_of_pattern == 0) {
        return failure_function;
      }

      // Base case invariant: The proper prefix of a single-character string is
      // empty.
      assert(failure_function[0] == 0
        && "The first element of the failure table must be 0.");

      auto length_of_previous_longest_prefix = 0;

      for (auto i = 1; i < length_of_pattern;) {
        // Invariant check: The prefix length being tested must always be less
        // than the current index.
        assert(length_of_previous_longest_prefix < i
          && "Candidate prefix length must be shorter than current substring.");
        assert(length_of_previous_longest_prefix >= 0
          && "Prefix length cannot be negative.");

        auto&& lhs = std::invoke(proj, *std::ranges::next(first, i));
        auto&& rhs = std::invoke(
          proj, *std::ranges::next(first, length_of_previous_longest_prefix));

        if (std::invoke(comp, lhs, rhs)) {
          // Extension found: P[i] == P[len]
          failure_function[i++] = ++length_of_previous_longest_prefix;
        } else if (length_of_previous_longest_prefix != 0) {
          // Mismatch: Fall back to the previous longest prefix that is also a
          // suffix.
          length_of_previous_longest_prefix =
            failure_function[length_of_previous_longest_prefix - 1];
        } else {
          // Mismatch and no previous prefix available: P[0...i] has no proper
          // prefix-suffix.
          failure_function[i++] = 0;
        }
      }

      // Post-condition check
      assert(std::cmp_equal(failure_function.size(), length_of_pattern)
        && "Result size must match input pattern size.");

      return failure_function;
    }

    /**
     * @brief Computes the KMP failure table for a generic range.
     *
     * @tparam Pattern The type of the input range. Must satisfy
     * `std::ranges::forward_range`.
     * @tparam Comp Binary predicate type for comparison. Defaults to
     * `std::equal_to<>`.
     * @tparam Proj Projection type to apply to elements before comparison.
     * Defaults to `std::identity`.
     *
     * @param pattern The input range representing the pattern.
     * @param comp Comparison predicate.
     * @param proj Projection to apply to elements.
     *
     * @return `std::vector<int>` containing the failure table.
     *
     * @see operator()(I first, S last, Comp comp, Proj proj)
     */
    template <std::ranges::forward_range Pattern,
      typename Comp = std::equal_to<>,
      typename Proj = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<std::ranges::iterator_t<Pattern>, Proj>,
        std::projected<std::ranges::iterator_t<Pattern>, Proj>>
    [[nodiscard]] static constexpr auto operator()(
      Pattern&& pattern, Comp comp = {}, Proj proj = {}) -> std::vector<int>
    {
      return operator()(
        std::ranges::begin(pattern), std::ranges::end(pattern), comp, proj);
    }
  } const knuth_morris_pratt_failure_function{};

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_FAILURE_FUNCTION_HPP
