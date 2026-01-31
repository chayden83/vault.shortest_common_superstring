// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP

#include <algorithm>
#include <cassert>
#include <concepts>
#include <functional>
#include <iterator>
#include <ranges>
#include <utility>
#include <vector>

#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>

namespace vault::algorithm {

  /**
   * @brief A function object searcher implementing the Knuth-Morris-Pratt (KMP)
   * algorithm.
   *
   * This class pre-processes a pattern to create a failure table (pi-table),
   * allowing for efficient searching in a text. It is compatible with
   * `std::search`.
   *
   * @tparam Pattern The range type of the pattern (Must satisfy
   * `std::ranges::forward_range`).
   * @tparam FailureTable The container type for the failure function (default:
   * std::vector<int>).
   * @tparam Comp Binary predicate for comparison (default: std::equal_to<>).
   * @tparam ProjPattern Projection applied to pattern elements (default:
   * std::identity).
   *
   * @par Complexity
   * @parblock
   * - **Preprocessing:** O(M) time and O(M) space, where M is the pattern
   * length.
   * - **Search:** O(N) time, where N is the text length.
   * - If `Pattern` is a **Random Access Range**, backtracking is O(1).
   * - If `Pattern` is only a **Forward Range**, backtracking requires iterating
   * from the beginning of the pattern, potentially adding overhead constants,
   * though the overall amortized complexity remains linear.
   * @endparblock
   *
   * @example
   * ```cpp
   * #include <string_view>
   * #include <iostream>
   * #include <vector>
   * #include <vault/algorithm/knuth_morris_pratt_searcher.hpp>
   *
   * int main() {
   * using namespace std::literals;
   * auto text = "ababcabcabababd"sv;
   * auto pattern = "ababd"sv;
   *
   * // Standard search
   * auto searcher = vault::algorithm::knuth_morris_pratt_searcher(pattern);
   * auto [match_begin, match_end] = searcher(text);
   *
   * if (match_begin != text.end()) {
   * std::cout << "Found match at index: " << std::distance(text.begin(),
   * match_begin) << "\n";
   * }
   *
   * // Case-insensitive search with custom predicate and projections
   * auto text2 = "HELLO WORLD"sv;
   * auto pattern2 = "world"sv;
   * auto searcher2 = vault::algorithm::knuth_morris_pratt_searcher(
   * pattern2,
   * std::equal_to<>{},
   * [](char c) { return std::tolower(c); } // Projection for pattern
   * );
   *
   * // Projection for text passed to operator()
   * auto [start, end] = searcher2(text2, [](char c) { return std::tolower(c);
   * });
   * }
   * ```
   */
  template <std::ranges::forward_range Pattern,
    knuth_morris_pratt_failure_table   FailureTable = std::vector<int>,
    typename Comp                                   = std::equal_to<>,
    typename ProjPattern                            = std::identity>
    requires std::indirect_binary_predicate<Comp,
      std::projected<std::ranges::iterator_t<Pattern>, ProjPattern>,
      std::projected<std::ranges::iterator_t<Pattern>, ProjPattern>>
  class knuth_morris_pratt_searcher {
    Pattern                           m_pattern;
    FailureTable                      m_failure_table;
    [[no_unique_address]] Comp        m_comp;
    [[no_unique_address]] ProjPattern m_proj_pattern;

  public:
    /**
     * @brief Constructs a searcher with a pre-computed failure table.
     *
     * @param pattern The pattern to search for.
     * @param failure_table A valid failure table corresponding to the pattern,
     * comparator, and projection.
     * @param comp Comparison function object.
     * @param proj_pattern Projection for pattern elements.
     */
    [[nodiscard]] constexpr knuth_morris_pratt_searcher(Pattern pattern,
      FailureTable                                              failure_table,
      Comp                                                      comp = {},
      ProjPattern proj_pattern                                       = {})
        : m_pattern{std::move(pattern)}
        , m_failure_table{std::move(failure_table)}
        , m_comp{std::move(comp)}
        , m_proj_pattern{std::move(proj_pattern)}
    {
      assert(
        std::cmp_equal(m_failure_table.size(), std::ranges::distance(m_pattern))
        && "Failure table size must match pattern size.");
    }

    /**
     * @brief Constructs a searcher and computes the failure table internally.
     *
     * @param pattern The pattern to search for.
     * @param comp Comparison function object.
     * @param proj_pattern Projection for pattern elements.
     */
    [[nodiscard]] constexpr knuth_morris_pratt_searcher(
      Pattern pattern, Comp comp = {}, ProjPattern proj_pattern = {})
        : m_pattern{std::move(pattern)}
        , m_failure_table{knuth_morris_pratt_failure_function(
            m_pattern, comp, proj_pattern)}
        , m_comp{std::move(comp)}
        , m_proj_pattern{std::move(proj_pattern)}
    {
      assert(std::cmp_equal(
        m_failure_table.size(), std::ranges::distance(m_pattern)));
    }

    /**
     * @brief Searches for the pattern in the given range [first, last).
     *
     * @tparam I Input iterator type for the Text.
     * @tparam S Sentinel type for the Text.
     * @tparam ProjText Projection type for the Text elements (default:
     * std::identity).
     *
     * @param first Iterator to the beginning of the text.
     * @param last Sentinel for the end of the text.
     * @param proj_text Projection applied to text elements before comparison.
     *
     * @return std::pair<I, I> Range defining the first occurrence of the
     * pattern. Returns {last, last} if not found.
     */
    template <std::forward_iterator I,
      std::sentinel_for<I>          S,
      typename ProjText = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<I, ProjText>,
        std::projected<std::ranges::iterator_t<Pattern>, ProjPattern>>
    [[nodiscard]] constexpr auto operator()(
      I first, S last, ProjText proj_text = {}) const -> std::pair<I, I>
    {
      if (std::ranges::empty(m_pattern)) {
        return {first, first};
      }

      auto pattern_index  = 0;
      auto pattern_first  = std::ranges::begin(m_pattern);
      auto pattern_cursor = pattern_first;
      auto pattern_length =
        std::ranges::distance(pattern_first, std::ranges::end(m_pattern));

      for (auto cursor = first; cursor != last; ++cursor) {

        auto check_match = [&]() {
          return std::invoke(m_comp,
            std::invoke(proj_text, *cursor),
            std::invoke(m_proj_pattern, *pattern_cursor));
        };

        // Backtrack
        while (pattern_index > 0 && !check_match()) {
          pattern_index = m_failure_table[pattern_index - 1];
          // std::next handles random access optimization internally
          pattern_cursor = std::next(pattern_first, pattern_index);
        }

        // Match check
        if (check_match()) {
          ++pattern_index;
          ++pattern_cursor;
        }

        // Complete match found
        if (pattern_index == pattern_length) {
          // Calculate start of match.
          // distance(first, cursor) is the index of the last matched char.
          auto cursor_dist      = std::ranges::distance(first, cursor);
          auto match_start_dist = cursor_dist - pattern_length + 1;

          auto match_first = std::next(first, match_start_dist);

          return {std::move(match_first), std::next(cursor)};
        }
      }

      return {last, last};
    }

    /**
     * @brief Range overload for the search operator.
     */
    template <std::ranges::forward_range Data,
      typename ProjText = std::identity>
      requires std::indirect_binary_predicate<Comp,
        std::projected<std::ranges::iterator_t<Data>, ProjText>,
        std::projected<std::ranges::iterator_t<Pattern>, ProjPattern>>
    [[nodiscard]] constexpr auto operator()(
      Data&& data, ProjText proj_text = {}) const
      -> std::pair<std::ranges::iterator_t<Data>, std::ranges::iterator_t<Data>>
    {
      return operator()(
        std::ranges::begin(data), std::ranges::end(data), std::move(proj_text));
    }
  };

  // --- Deduction Guides ---

  template <std::ranges::forward_range Pattern,
    knuth_morris_pratt_failure_table   FailureTable,
    typename Comp        = std::equal_to<>,
    typename ProjPattern = std::identity>
  knuth_morris_pratt_searcher(
    Pattern&&, FailureTable&&, Comp = {}, ProjPattern = {})
    -> knuth_morris_pratt_searcher<std::remove_cvref_t<Pattern>,
      std::remove_cvref_t<FailureTable>,
      Comp,
      ProjPattern>;

  template <std::ranges::forward_range Pattern,
    typename Comp        = std::equal_to<>,
    typename ProjPattern = std::identity>
  knuth_morris_pratt_searcher(Pattern&&, Comp = {}, ProjPattern = {})
    -> knuth_morris_pratt_searcher<std::remove_cvref_t<Pattern>,
      std::vector<int>,
      Comp,
      ProjPattern>;

  /**
   * @brief Helper factory to create a searcher with automatic type deduction.
   */
  constexpr inline struct make_knuth_morris_pratt_searcher_fn {
    [[nodiscard]] static constexpr auto operator()(auto&&... args)
      -> decltype(knuth_morris_pratt_searcher{
        std::forward<decltype(args)>(args)...})
    {
      return knuth_morris_pratt_searcher{std::forward<decltype(args)>(args)...};
    }
  } const make_knuth_morris_pratt_searcher{};

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP
