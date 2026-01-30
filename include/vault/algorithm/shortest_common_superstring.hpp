// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
#define VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP

#include <algorithm>
#include <concepts>
#include <functional>
#include <iterator>
#include <ranges>
#include <string>
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
     */
    static constexpr auto strlen_fn = []<typename T>(
                                        T const& t) -> std::size_t {
      return std::ranges::size(t);
    };

  public:
    /**
     * @brief Return type containing the superstring and mapping information.
     *
     * @tparam In Iterator type of the input range.
     * @tparam Out Iterator type of the output range.
     * @tparam SuperString The type of the resulting superstring.
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

    /**
     * @brief Calculates the greedy shortest common superstring.
     *
     * @tparam R The type of the input range (must satisfy forward_range).
     * @tparam O The type of the output iterator.
     * @param strings The input collection of strings.
     * @param out An output iterator to write the `bounds_t` (offset, length)
     * for each input string.
     * @return result struct containing the superstring and iteration state.
     */
    template <std::ranges::forward_range R, std::output_iterator<bounds_t<R>> O>
    [[nodiscard]]
    auto operator()(R&& strings, O out) const
      -> result<std::ranges::iterator_t<R>, O, std::ranges::range_value_t<R>>
    {
      if (std::ranges::empty(strings)) {
        return {std::ranges::end(strings), out, {}, 0};
      }

      // 1. Preprocessing: Compute Failure Functions & Filter Substrings
      auto ftables = strings
        | ::ranges::views::transform(knuth_morris_pratt_failure_function)
        | ::ranges::to<std::vector>();

      auto string_ptrs =
        ::ranges::to<std::vector>(::ranges::views::addressof(strings));
      auto ftable_ptrs =
        ::ranges::to<std::vector>(::ranges::views::addressof(ftables));

      auto total_overlap = std::size_t{0};

      // Scoped block for filtering logic
      std::invoke([&] {
        // Sort by length (Ascending).
        std::ranges::sort(::ranges::views::zip(string_ptrs, ftable_ptrs),
          {},
          [](auto const& pair) { return strlen_fn(*pair.first); });

        auto out_strings = string_ptrs.begin();
        auto out_ftables = ftable_ptrs.begin();

        auto in_strings = string_ptrs.begin();
        auto in_ftables = ftable_ptrs.begin();

        while (in_strings != string_ptrs.end()) {
          auto searcher =
            knuth_morris_pratt_searcher{**in_strings, **in_ftables};

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

      // 2. Build Overlap Graph
      auto reduced_strings =
        ::ranges::to<std::vector>(::ranges::views::indirect(string_ptrs));

      // Note: std::vector<bool> specialization makes list initialization tricky
      // with size/value. Using parenthesis for the constructor here is
      // safer/clearer than braces to avoid initializer_list ambiguity.
      auto is_active_string = std::vector<bool>(reduced_strings.size(), true);

      auto index = overlap_graph_t{};

      for (auto i = std::size_t{0}; i < reduced_strings.size(); ++i) {
        for (auto j = std::size_t{0}; j < reduced_strings.size(); ++j) {
          if (i == j) {
            continue;
          }

          auto score = knuth_morris_pratt_overlap(
            reduced_strings[i], reduced_strings[j], *ftable_ptrs[j])
                         .score;

          if (score > 0) {
            index.emplace(i, j, score);
          }
        }
      }

      // 3. Greedy Merge Loop
      while (!index.empty()) {
        auto const entry = *index.get<tag_score>().begin();
        index.get<tag_score>().erase(index.get<tag_score>().begin());

        auto const lhs     = entry.lhs;
        auto const rhs     = entry.rhs;
        auto const overlap = entry.score;

        reduced_strings[lhs].append(reduced_strings[rhs], overlap);

        total_overlap += overlap;
        is_active_string[rhs] = false;

        // --- Update Graph ---
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

      // 4. Final Assembly
      using SuperStringT = std::remove_cvref_t<decltype(reduced_strings[0])>;
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
          final_superstring += reduced_strings[i];
        }
      }

      // 5. Final Position Mapping
      auto super_begin = std::ranges::begin(final_superstring);
      auto super_end   = std::ranges::end(final_superstring);

      for (auto&& [substring, ftable] :
        ::ranges::views::zip(strings, ftables)) {
        auto searcher =
          knuth_morris_pratt_searcher{substring, std::move(ftable)};

        auto match = std::search(super_begin, super_end, searcher);

        if (match == super_end) {
          *out++ = bounds_t<R>{-1, 0};
        } else {
          auto offset = std::ranges::distance(super_begin, match);
          *out++      = bounds_t<R>{offset, substring.size()};
        }
      }

      return {std::ranges::end(strings),
        out,
        std::move(final_superstring),
        total_overlap};
    }
  };

  constexpr inline auto greedy_shortest_common_superstring =
    greedy_shortest_common_superstring_fn{};

  // Backward compatibility alias for existing benchmarks and tests
  // This resolves the "no member named 'shortest_common_superstring'" errors.
  constexpr inline auto shortest_common_superstring =
    greedy_shortest_common_superstring;

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
