// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
#define VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/addressof.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>

#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>
#include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
#include <vault/algorithm/knuth_morris_pratt_searcher.hpp>

namespace vault::algorithm {

  /**
   * @brief Computes an approximation of the Shortest Common Superstring (SCS)
   * using a Greedy strategy.
   *
   * This functor implements the standard Greedy SCS heuristic:
   * 1. Calculate the overlap between all pairs of strings.
   * 2. Repeatedly merge the pair of strings with the maximum overlap.
   * 3. If overlaps are zero, strings are concatenated.
   *
   * @note The SCS problem is NP-hard. This greedy approximation has a
   * conjectured approximation ratio of 2 and a proven ratio of 4 (or 2.5 in
   * specific variants). It does not guarantee the optimal solution.
   */
  class greedy_shortest_common_superstring_fn {
    /**
     * @brief Internal structure to represent an edge in the overlap graph.
     */
    struct index_entry_t {
      /// The index of the string acting as the prefix (Left Hand Side).
      std::size_t lhs;
      /// The index of the string acting as the suffix (Right Hand Side).
      std::size_t rhs;
      /// The length of the overlap between lhs and rhs.
      std::size_t score;
    };

    // Tags for Boost.MultiIndex
    struct tag_lhs {};

    struct tag_rhs {};

    struct tag_score {};

    /**
     * @brief A multi-indexed container storing the overlap graph edges.
     *
     * Indices:
     * - tag_score: Ordered non-unique (descending) to efficiently retrieve max
     * overlap.
     * - tag_lhs: Hashed to efficiently update edges originating from a merged
     * string.
     * - tag_rhs: Hashed to efficiently remove edges targeting a consumed
     * string.
     */
    using overlap_graph_t = boost::multi_index_container<index_entry_t,
      boost::multi_index::indexed_by<
        boost::multi_index::ordered_non_unique<
          boost::multi_index::tag<tag_score>,
          boost::multi_index::
            member<index_entry_t, std::size_t, &index_entry_t::score>,
          std::greater<std::size_t>>,
        boost::multi_index::hashed_non_unique<boost::multi_index::tag<tag_lhs>,
          boost::multi_index::
            member<index_entry_t, std::size_t, &index_entry_t::lhs>>,
        boost::multi_index::hashed_non_unique<boost::multi_index::tag<tag_rhs>,
          boost::multi_index::
            member<index_entry_t, std::size_t, &index_entry_t::rhs>>>>;

    /**
     * @brief Helper lambda to calculate the size of a range.
     * Uses std::ranges::distance to support non-sized ranges.
     */
    static constexpr auto strlen_fn = []<typename T>(
                                        T const& t) -> std::size_t {
      return std::ranges::distance(t);
    };

  public:
    /**
     * @brief Return type containing the superstring and mapping information.
     *
     * @tparam In Iterator type of the input range.
     * @tparam Out Iterator type of the output range.
     * @tparam SuperString The type of the resulting superstring (always a
     * std::vector).
     * @tparam Overlap The type used to measure overlap (usually size_t).
     */
    template <typename In,
      typename Out,
      typename SuperString,
      typename Overlap = std::size_t>
    struct result {
      In          in;            ///< Iterator to the end of the input range.
      Out         out;           ///< Iterator to the end of the output range.
      SuperString superstring;   ///< The constructed common superstring.
      Overlap     total_overlap; ///< The sum of all overlaps utilized.
    };

    /**
     * @brief A pair representing the [offset, length] of an input string within
     * the superstring.
     */
    template <std::ranges::range R>
    using bounds_t = std::pair<std::ranges::range_difference_t<R>,
      std::ranges::range_size_t<R>>;

    // =========================================================================
    // Overload 1: Raw / No Projection
    // =========================================================================

    /**
     * @brief Calculates the greedy shortest common superstring for a range of
     * generic sequences.
     *
     * This overload operates directly on the input elements (or views thereof).
     *
     * @tparam R The type of the input range (must satisfy forward_range).
     * @tparam Out The type of the output iterator.
     * @tparam Comp The comparator type for elements (defaults to
     * std::equal_to).
     *
     * @param strings The input collection of sequences.
     * @param out An output iterator to write the `bounds_t` (offset, length).
     * @param comp Comparison function object.
     *
     * @return result struct containing the superstring.
     *
     * @par Complexity
     * @parblock
     * - **Time:** O(N^2 * L) where N is the number of strings and L is the max
     * string length. If the input ranges are not Random Access, performance
     * degrades due to O(L) traversal costs.
     * - **Space:** O(N * L) to store the superstring and internal structures.
     * @endparblock
     */
    template <std::ranges::forward_range R,
      typename Out,
      typename Comp = std::equal_to<>>
      requires std::indirect_binary_predicate<Comp,
        std::ranges::iterator_t<std::ranges::range_value_t<R>>,
        std::ranges::iterator_t<std::ranges::range_value_t<R>>>
    [[nodiscard]]
    auto operator()(R&& strings, Out out, Comp comp = {}) const
    {
      // Use range_reference_t to capture 'const T&' correctly, ensuring
      // ref_view works for const inputs.
      using InputString = std::ranges::range_reference_t<R>;
      using Element     = std::ranges::range_value_t<InputString>;

      // The superstring is always a vector of elements to ensure ownership and
      // random access.
      using SuperStringT = std::vector<Element>;

      if (std::ranges::empty(strings)) {
        return result<std::ranges::iterator_t<R>, Out, SuperStringT>{
          std::ranges::end(strings), out, {}, 0};
      }

      // --- Step 1: Reduction Logic (L-value vs R-value handling) ---

      // If the input range returns L-value references (e.g., standard
      // container), we use ref_view to avoid copying strings during the
      // filtering phase. If it returns values (e.g., transform_view or R-value
      // refs), we must materialize and hold the value.
      using ReductionString =
        std::conditional_t<std::is_lvalue_reference_v<InputString>,
          std::ranges::ref_view<std::remove_reference_t<InputString>>,
          std::remove_cvref_t<InputString>>;

      // Materialize the input for sorting/filtering.
      // This is our "working set" for the reduction phase.
      // Using ranges::to<vector> handles both views and values correctly.
      auto working_set = strings | ::ranges::to<std::vector<ReductionString>>();

      // Precompute failure tables for the reduction phase
      auto ftables = working_set | ::ranges::views::transform([&](auto&& s) {
        return knuth_morris_pratt_failure_function(s, comp);
      }) | ::ranges::to<std::vector>();

      // Create pointers to keep sorting cheap
      auto string_ptrs =
        ::ranges::to<std::vector>(::ranges::views::addressof(working_set));
      auto ftable_ptrs =
        ::ranges::to<std::vector>(::ranges::views::addressof(ftables));

      auto total_overlap = std::size_t{0};

      // --- Step 2: Filtering Substrings ---

      std::invoke([&] {
        // Sort by length (Ascending)
        std::ranges::sort(::ranges::views::zip(string_ptrs, ftable_ptrs),
          {},
          [](auto const& pair) { return strlen_fn(*pair.first); });

        auto out_strings = string_ptrs.begin();
        auto out_ftables = ftable_ptrs.begin();
        auto in_strings  = string_ptrs.begin();
        auto in_ftables  = ftable_ptrs.begin();

        while (in_strings != string_ptrs.end()) {
          // Note: Construct searcher with the custom comparator
          auto searcher =
            knuth_morris_pratt_searcher{**in_strings, **in_ftables, comp};

          auto is_superstring = [&](auto* haystrand_ptr) {
            return std::search(std::ranges::begin(*haystrand_ptr),
                     std::ranges::end(*haystrand_ptr),
                     searcher)
              != std::ranges::end(*haystrand_ptr);
          };

          if (std::ranges::none_of(std::ranges::next(in_strings),
                string_ptrs.end(),
                is_superstring)) {
            *out_strings++ = *in_strings;
            *out_ftables++ = *in_ftables;
          } else {
            total_overlap += strlen_fn(**in_strings);
          }
          ++in_strings;
          ++in_ftables;
        }

        string_ptrs.erase(out_strings, string_ptrs.end());
        ftable_ptrs.erase(out_ftables, ftable_ptrs.end());
      });

      // --- Step 3: Materialize Survivors for Merging ---

      // We now deep copy the survivors into the final owning container type
      // (vector<vector<Element>>). This allows us to modify (append to) them
      // safely without affecting the original input or dealing with view
      // constness.
      auto reduced_strings = string_ptrs
        | ::ranges::views::indirect
        // Explicitly transform each element to SuperStringT (vector<Element>)
        | ::ranges::views::transform(
          [](auto&& r) { return r | ::ranges::to<SuperStringT>(); })
        | ::ranges::to<std::vector>();

      // Track active strings (those that haven't been appended to another)
      // Using vector<bool> as requested (std::vector<bool> uses 1 bit per
      // element).
      auto is_active_string = std::vector<bool>(reduced_strings.size(), true);
      auto index            = overlap_graph_t{};

      // --- Step 4: Build Overlap Graph ---

      for (auto i = std::size_t{0}; i < reduced_strings.size(); ++i) {
        for (auto j = std::size_t{0}; j < reduced_strings.size(); ++j) {
          if (i == j) {
            continue;
          }

          // Pass comparator to overlap function
          auto score = knuth_morris_pratt_overlap(
            reduced_strings[i], reduced_strings[j], *ftable_ptrs[j], comp)
                         .score;

          if (score > 0) {
            index.emplace(i, j, static_cast<std::size_t>(score));
          }
        }
      }

      // --- Step 5: Greedy Merge Loop ---

      while (!index.empty()) {
        auto const entry = *index.get<tag_score>().begin();
        index.get<tag_score>().erase(index.get<tag_score>().begin());

        auto const lhs     = entry.lhs;
        auto const rhs     = entry.rhs;
        auto const overlap = entry.score;

        // Merge: Append the non-overlapping suffix of RHS to LHS
        // We use generic range insertion.
        auto& lhs_str = reduced_strings[lhs];
        auto& rhs_str = reduced_strings[rhs];

        // Calculate suffix start: begin + overlap
        auto suffix_begin =
          std::ranges::next(std::ranges::begin(rhs_str), overlap);
        auto suffix_end = std::ranges::end(rhs_str);

        lhs_str.insert(lhs_str.end(),
          std::make_move_iterator(suffix_begin),
          std::make_move_iterator(suffix_end));

        total_overlap += overlap;
        is_active_string[rhs] = false;

        // Update Graph
        index.get<tag_lhs>().erase(lhs);
        index.get<tag_rhs>().erase(rhs);

        auto& lhs_index = index.get<tag_lhs>();
        auto  range     = lhs_index.equal_range(rhs);

        auto it = range.first;
        while (it != range.second) {
          auto current = it++;
          if (current->rhs != lhs) {
            index.emplace(lhs, current->rhs, current->score);
          }
          lhs_index.erase(current);
        }
      }

      // --- Step 6: Final Assembly ---

      auto final_superstring = SuperStringT{};

      auto estimated_size = std::size_t{0};
      for (auto i = std::size_t{0}; i < reduced_strings.size(); ++i) {
        if (is_active_string[i]) {
          estimated_size += reduced_strings[i].size();
        }
      }
      final_superstring.reserve(estimated_size);

      for (auto i = std::size_t{0}; i < reduced_strings.size(); ++i) {
        if (is_active_string[i]) {
          final_superstring.insert(final_superstring.end(),
            std::make_move_iterator(reduced_strings[i].begin()),
            std::make_move_iterator(reduced_strings[i].end()));
        }
      }

      // --- Step 7: Final Position Mapping ---

      auto super_begin = std::ranges::begin(final_superstring);
      auto super_end   = std::ranges::end(final_superstring);

      // Re-scan original strings to find their positions in the superstring.
      for (auto&& substring : strings) {
        // We assume the user wants the mapping based on the *original*
        // comparator.
        auto searcher = knuth_morris_pratt_searcher{substring, comp};

        auto match = std::search(super_begin, super_end, searcher);

        if (match == super_end) {
          *out++ = bounds_t<R>{-1, 0};
        } else {
          auto offset = std::ranges::distance(super_begin, match);
          *out++      = bounds_t<R>{offset, std::ranges::distance(substring)};
        }
      }

      return result<std::ranges::iterator_t<R>, Out, SuperStringT>{
        std::ranges::end(strings),
        out,
        std::move(final_superstring),
        total_overlap};
    }

    // =========================================================================
    // Overload 2: With Projection
    // =========================================================================

    /**
     * @brief Calculates the greedy SCS with a projection applied to inputs.
     *
     * This overload projects all input sequences into a cached structure
     * before invoking the greedy algorithm. This avoids repeated expensive
     * projections.
     *
     * @tparam R The type of the input range.
     * @tparam Out The type of the output iterator.
     * @tparam Proj The projection type (must be callable).
     * @tparam Comp The comparator type.
     *
     * @param strings The input collection.
     * @param out Output iterator.
     * @param proj Projection function.
     * @param comp Comparison function.
     *
     * @return result struct containing the projected superstring.
     */
    template <std::ranges::forward_range R,
      std::forward_iterator              Out,
      typename Proj,
      typename Comp = std::equal_to<>>
      requires std::indirectly_unary_invocable<Proj,
                 std::ranges::iterator_t<std::ranges::range_value_t<R>>>
      && std::indirect_binary_predicate<Comp,
        std::invoke_result_t<Proj,
          std::iter_reference_t<
            std::ranges::iterator_t<std::ranges::range_value_t<R>>>>,
        std::invoke_result_t<Proj,
          std::iter_reference_t<
            std::ranges::iterator_t<std::ranges::range_value_t<R>>>>>
    [[nodiscard]]
    auto operator()(R&& strings, Out out, Proj proj, Comp comp = {}) const
    {
      // Deduce the projected value type
      using InputString = std::ranges::range_reference_t<R>;
      using InputIter =
        std::ranges::iterator_t<std::remove_cvref_t<InputString>>;

      // Note: std::invoke_result_t gives the raw return type.
      // We strip cv-ref to store it in a vector (Cache).
      using ProjectedValue = std::remove_cvref_t<
        std::invoke_result_t<Proj, std::iter_reference_t<InputIter>>>;

      // Materialize the projected inputs into a vector of vectors.
      // This is our "Cache".
      // We use vector<ProjectedValue> as the canonical sequence container.
      using CachedString = std::vector<ProjectedValue>;

      auto cached_strings = std::vector<CachedString>{};
      cached_strings.reserve(std::ranges::distance(strings));

      for (auto&& s : strings) {
        auto projected_s = s | ::ranges::views::transform(proj)
          | ::ranges::to<CachedString>();
        cached_strings.push_back(std::move(projected_s));
      }

      // Delegate to the raw overload using the cached strings.
      // Note: We pass a temporary vector, so it will use the "Value" reduction
      // path inside Overload 1, effectively moving our cached strings into the
      // working set.
      auto res = operator()(cached_strings, out, comp);

      // The result structure from the delegate corresponds to the *cached*
      // strings. We need to adjust the return type to match the *original*
      // input iterators 'R'. However, since 'res.in' is just an iterator to the
      // end, and we consumed 'strings' fully, we can construct the result
      // manually.
      return result<std::ranges::iterator_t<R>,
        Out,
        std::vector<ProjectedValue>>{std::ranges::end(strings),
        res.out,
        std::move(res.superstring),
        res.total_overlap};
    }
  };

  constexpr inline auto greedy_shortest_common_superstring =
    greedy_shortest_common_superstring_fn{};

  // Backward compatibility alias
  constexpr inline auto shortest_common_superstring =
    greedy_shortest_common_superstring;

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
