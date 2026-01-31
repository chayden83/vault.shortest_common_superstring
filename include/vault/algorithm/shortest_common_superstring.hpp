// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
#define VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP

#include <algorithm>
#include <cassert>
#include <concepts>
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

  namespace detail {
    // Extract the iterator type of the *inner* range.
    template <typename R>
    using inner_iterator_t = std::ranges::iterator_t<
      std::remove_cvref_t<std::ranges::range_reference_t<R>>>;

    // The result of projecting an element of the inner range.
    template <typename Proj, typename R>
    using inner_projected_value_t =
      std::remove_cvref_t<std::indirect_result_t<Proj, inner_iterator_t<R>>>;
  } // namespace detail

  // --- reusable concepts ---

  // Verifies that Comp is a valid binary predicate for the elements of the
  // inner ranges.
  template <typename Comp, typename R>
  concept inner_element_comparator = std::indirect_binary_predicate<Comp,
    detail::inner_iterator_t<R>,
    detail::inner_iterator_t<R>>;

  // Verifies that Proj is callable on the elements of the inner ranges.
  template <typename Proj, typename R>
  concept inner_element_projector =
    std::indirectly_unary_invocable<Proj, detail::inner_iterator_t<R>>;

  // Verifies that Comp can compare the results of Proj applied to inner
  // elements.
  template <typename Comp, typename Proj, typename R>
  concept projected_inner_element_comparator =
    std::indirect_binary_predicate<Comp,
      detail::inner_projected_value_t<Proj, R>*,
      detail::inner_projected_value_t<Proj, R>*>;

  // Verifies that Out is an output iterator capable of accepting a subrange of
  // a vector of ValueType.
  template <typename Out, typename ValueType>
  concept vector_subrange_output_iterator = std::output_iterator<Out,
    std::ranges::subrange<typename std::vector<ValueType>::iterator>>;

  /**
   * @brief Computes an approximation of the Shortest Common Superstring (SCS)
   * using a Greedy strategy.
   */
  class greedy_shortest_common_superstring_fn {
    struct index_entry_t {
      std::size_t lhs;
      std::size_t rhs;
      std::size_t score;
    };

    struct tag_lhs {};

    struct tag_rhs {};

    struct tag_score {};

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

    static constexpr auto strlen_fn = []<typename T>(
                                        T const& t) -> std::size_t {
      return std::ranges::distance(t);
    };

  public:
    template <typename In,
      typename Out,
      typename SuperString,
      typename Overlap = std::size_t>
    struct result {
      In          in;
      Out         out;
      SuperString superstring;
      Overlap     total_overlap;
    };

    // =========================================================================
    // Overload 1: Raw / No Projection
    // =========================================================================

    template <std::ranges::forward_range R,
      typename Out,
      typename Comp = std::equal_to<>>
      requires inner_element_comparator<Comp, R>
      && vector_subrange_output_iterator<Out,
        std::iter_value_t<detail::inner_iterator_t<R>>>
    [[nodiscard]]
    auto operator()(R&& strings, Out out, Comp comp = {}) const
      -> result<std::ranges::iterator_t<R>,
        Out,
        std::vector<std::iter_value_t<detail::inner_iterator_t<R>>>>
    {
      using InputString  = std::ranges::range_reference_t<R>;
      using Element      = std::ranges::range_value_t<InputString>;
      using SuperStringT = std::vector<Element>;

      if (std::ranges::empty(strings)) {
        return result<std::ranges::iterator_t<R>, Out, SuperStringT>{
          std::ranges::end(strings), out, {}, 0};
      }

      // Reduction Logic
      using ReductionString =
        std::conditional_t<std::is_lvalue_reference_v<InputString>,
          std::ranges::ref_view<std::remove_reference_t<InputString>>,
          std::remove_cvref_t<InputString>>;

      auto working_set = strings | ::ranges::to<std::vector<ReductionString>>();

      auto ftables = working_set | ::ranges::views::transform([&](auto&& s) {
        return knuth_morris_pratt_failure_function(s, comp);
      }) | ::ranges::to<std::vector>();

      auto string_ptrs =
        ::ranges::to<std::vector>(::ranges::views::addressof(working_set));
      auto ftable_ptrs =
        ::ranges::to<std::vector>(::ranges::views::addressof(ftables));

      auto total_overlap = std::size_t{0};

      // Filtering Substrings
      std::invoke([&] {
        std::ranges::sort(::ranges::views::zip(string_ptrs, ftable_ptrs),
          {},
          [](auto const& pair) { return strlen_fn(*pair.first); });

        auto out_strings = string_ptrs.begin();
        auto out_ftables = ftable_ptrs.begin();
        auto in_strings  = string_ptrs.begin();
        auto in_ftables  = ftable_ptrs.begin();

        while (in_strings != string_ptrs.end()) {
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

      // Materialize Survivors
      auto reduced_strings = string_ptrs | ::ranges::views::indirect
        | ::ranges::views::transform(
          [](auto&& r) { return r | ::ranges::to<SuperStringT>(); })
        | ::ranges::to<std::vector>();

      auto is_active_string = std::vector<bool>(reduced_strings.size(), true);
      auto index            = overlap_graph_t{};

      // Build Graph
      for (auto i = std::size_t{0}; i < reduced_strings.size(); ++i) {
        for (auto j = std::size_t{0}; j < reduced_strings.size(); ++j) {
          if (i == j) {
            continue;
          }
          auto score = knuth_morris_pratt_overlap(
            reduced_strings[i], reduced_strings[j], *ftable_ptrs[j], comp)
                         .score;
          if (score > 0) {
            index.emplace(i, j, static_cast<std::size_t>(score));
          }
        }
      }

      // Greedy Merge
      while (!index.empty()) {
        auto const entry = *index.get<tag_score>().begin();
        index.get<tag_score>().erase(index.get<tag_score>().begin());

        auto const lhs     = entry.lhs;
        auto const rhs     = entry.rhs;
        auto const overlap = entry.score;

        auto& lhs_str = reduced_strings[lhs];
        auto& rhs_str = reduced_strings[rhs];

        auto suffix_begin =
          std::ranges::next(std::ranges::begin(rhs_str), overlap);
        auto suffix_end = std::ranges::end(rhs_str);

        lhs_str.insert(lhs_str.end(),
          std::make_move_iterator(suffix_begin),
          std::make_move_iterator(suffix_end));

        total_overlap += overlap;
        is_active_string[rhs] = false;

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

      // Final Assembly
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

      // Position Mapping using Subranges
      // Note: std::vector iterators remain valid after the move constructor.
      auto super_begin = std::ranges::begin(final_superstring);
      auto super_end   = std::ranges::end(final_superstring);

      for (auto&& substring : strings) {
        auto searcher = knuth_morris_pratt_searcher{substring, comp};
        auto match    = std::search(super_begin, super_end, searcher);

        if (match == super_end) {
          // Empty subrange at end
          *out++ = std::ranges::subrange(super_end, super_end);
        } else {
          auto length = std::ranges::distance(substring);
          *out++ =
            std::ranges::subrange(match, std::ranges::next(match, length));
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

    template <std::ranges::forward_range R,
      typename Out,
      typename Proj,
      typename Comp = std::equal_to<>>
      requires inner_element_projector<Proj, R>
      && projected_inner_element_comparator<Comp, Proj, R>
      && vector_subrange_output_iterator<Out,
        detail::inner_projected_value_t<Proj, R>>
    [[nodiscard]]
    auto operator()(R&& strings, Out out, Proj proj, Comp comp = {}) const
      -> result<std::ranges::iterator_t<R>,
        Out,
        std::vector<detail::inner_projected_value_t<Proj, R>>>
    {
      using ProjectedValue = detail::inner_projected_value_t<Proj, R>;
      using CachedString   = std::vector<ProjectedValue>;

      auto cached_strings = std::vector<CachedString>{};
      cached_strings.reserve(std::ranges::distance(strings));

      for (auto&& s : strings) {
        auto projected_s = s | ::ranges::views::transform(proj)
          | ::ranges::to<CachedString>();
        cached_strings.push_back(std::move(projected_s));
      }

      auto res = operator()(cached_strings, out, comp);

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
  constexpr inline auto shortest_common_superstring =
    greedy_shortest_common_superstring;

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
