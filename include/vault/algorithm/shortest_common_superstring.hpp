// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
#define VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP

#include <tuple>
#include <string>
#include <ranges>
#include <utility>
#include <iterator>
#include <algorithm>

#include <boost/multi_index_container.hpp>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <range/v3/range/conversion.hpp>

#include <range/v3/view/zip.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/generate.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>

#include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
#include <vault/algorithm/knuth_morris_pratt_searcher.hpp>
#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>

// clang-format off

namespace vault::algorithm {
  constexpr inline struct proper_suffixes_fn {
    template<std::forward_iterator I, std::sentinel_for<I> S>
    [[nodiscard]] static constexpr auto operator ()(I first, S last) {
      auto generator = [=]() mutable -> std::ranges::subrange<I, S> {
        return std::ranges::subrange { first == last ? first : ++first, last };
      };

      return ::ranges::views::generate(std::move(generator));
    }

    [[nodiscard]] static constexpr auto operator ()(std::ranges::forward_range auto &&range) {
      return operator ()(std::ranges::begin(range), std::ranges::end(range));
    }
  } const proper_suffixes { };

  constexpr inline struct shortest_common_superstring_fn {
    static constexpr inline auto const filter_fn = []
      (auto const &parameters) -> bool
    {
      auto const &[needle, haystack, failure_table] = parameters;

      auto searcher = knuth_morris_pratt_searcher(needle, failure_table);

      return std::ranges::none_of(haystack, [&](auto const &haystrand) -> bool {
	auto match_itr = std::search
	  (std::ranges::begin(haystrand), std::ranges::end(haystrand), searcher);

	return match_itr != std::ranges::end(haystrand);
      });
    };

    static constexpr inline auto const length_fn = []<std::ranges::range R>(R const &range) {
      if constexpr (std::ranges::sized_range<R>) {
	return std::ranges::size(range);
      } else {
	return std::ranges::distance(std::ranges::begin(range), std::ranges::end(range));
      }
    };

    struct overlap_entry_t {
      std::string lhs = {};
      std::string rhs = {};

      int score = 0;
    };

    struct overlap_lhs_t { };
    struct overlap_rhs_t { };

    struct overlap_score_t { };

    using overlap_index_t = boost::multi_index_container<
      overlap_entry_t, boost::multi_index::indexed_by<
        boost::multi_index::ordered_non_unique<
	  boost::multi_index::tag<overlap_score_t>,
	  boost::multi_index::member<overlap_entry_t, int, &overlap_entry_t::score>,
	  std::greater<>
	>,
        boost::multi_index::hashed_non_unique<
	  boost::multi_index::tag<overlap_lhs_t>,
	  boost::multi_index::member<overlap_entry_t, std::string, &overlap_entry_t::lhs>
	>,
        boost::multi_index::hashed_non_unique<
	  boost::multi_index::tag<overlap_rhs_t>,
	  boost::multi_index::member<overlap_entry_t, std::string, &overlap_entry_t::rhs>
	>
      >
    >;

    template<typename In, typename Out, typename SuperString, typename Overlap = int>
    struct result {
      In  in;
      Out out;

      SuperString superstring;
      Overlap     overlap;
    };

    template<std::ranges::range R>
    using bounds_t = std::pair
      <std::ranges::range_difference_t<R>, std::ranges::range_size_t<R>>;

    template<std::ranges::forward_range R, std::output_iterator<bounds_t<R>> O>
    static auto operator ()(R &&range, O out) ->
      result<std::ranges::iterator_t<R>, O, std::ranges::range_value_t<R>>
    {
      // Sort the input elements by length. This allows us to reduce
      // the number of elements we need to check when filtering each
      // element that is a substring of another element.
      std::ranges::sort(range, {}, length_fn);

      // Preconstruct the failure tables, cache them, and create a
      // random access range that returns a view of the failure table
      // for the nth input element on demand. We use views of the
      // faulure tables whenever possible to avoid copies and
      // recalculations.
      auto failure_tables = range
	| ::ranges::views::transform(knuth_morris_pratt_failure_function)
	| ::ranges::to<std::vector>();

      // A random access range to return a searcher for the nth input
      // element. Constructon is cheap because we construct the
      // searcher from element and failure table views, so we
      // construct the searchers on demand instead of caching them.
      auto searchers = ::ranges::views::zip_with
	(make_knuth_morris_pratt_searcher, range, failure_tables);







      auto to_pattern_failure_table_pair = [](auto &&parameters) {
	auto &&[pattern, _, failure_table] = parameters;

	return std::pair {
	  std::forward<decltype(pattern      )>(pattern      ),
	  std::forward<decltype(failure_table)>(failure_table)
	};
      };

      auto filtered = ::ranges::views::zip(range, proper_suffixes(range), failure_tables)
        | ::ranges::views::filter(filter_fn)
        | ::ranges::views::transform(to_pattern_failure_table_pair)
        | ::ranges::to<std::vector>();

      auto index = overlap_index_t { };

      // TODO: Convert to a view that emits off-diagonal pairs.
      for(auto i = 0uz; i < filtered.size(); ++i) {
        for(auto j = 0uz; j < filtered.size(); ++j) {
	  if(i == j) continue;

	  auto const &[pattern_i, _              ] = filtered[i];
	  auto const &[pattern_j, failure_table_j] = filtered[j];

	  auto score = knuth_morris_pratt_overlap
	    (pattern_i, pattern_j, failure_table_j).score;

	  index.emplace(pattern_i, pattern_j, score);
        }
      }

      if(filtered.size() == 1) {
	index.emplace(filtered[0].first, filtered[0].first, filtered[0].first.size());
      }

      // TODO: Include cumulative length of words that are proper
      // sub-strings in cumulative overlap.
      auto cum_overlap = 0;
      auto superstring = std::string { };

      // Repeatedly merge the two index entries with the largest
      // overlap until we have a single entry. This involves the
      // following bookkeeping.
      //
      // - Extract (and therefore erase) the entry containing the two
      //   elements with the largest overlap.
      // - Add the overlap of the entry to the cumulative overlap.
      // - Calculate the superstring for the two elements in the
      //   entry.
      // - Create new entries for the superstring as necessary.
      // - Erase every entry that refers to either of the strings in
      //   the entry with the max overlap.
      //
      // Take care not to update the [rhs, lhs, ...] index entry to
      // [superstring, superstring, ...] or that node will not be
      // erased as intended and bad things will happen.
      while(index.size() != 0) {
        auto [lhs, rhs, overlap] = index.get<overlap_score_t>()
	  .extract(index.get<overlap_score_t>().begin()).value();

	cum_overlap += overlap;
        superstring  = lhs;

	superstring.append(rhs, overlap);

	// We previously filtered the elements that are proper
	// substrings of another element, so we know the maximum
	// overlap between any two strings s1 and s2 if min(len(s1) -
	// 1, len(s2) - 1). This means that the merged string can
	// never produce a new overlap that is larger than the
	// existing overlap. Consequently, we can propagate the
	// existing overlaps and we never need to perform another
	// overlap calculation.
        for(auto [first, last] = index.get<overlap_lhs_t>().equal_range(rhs); first != last; ++first) {
	  if(first -> rhs != lhs) index.emplace(superstring, first -> rhs, first -> score);
        }

        for(auto [first, last] = index.get<overlap_rhs_t>().equal_range(lhs); first != last; ++first) {
	  if(first -> lhs != rhs) index.emplace(first -> lhs, superstring, first -> score);
        }

	index.get<overlap_lhs_t>().erase(lhs);
	index.get<overlap_lhs_t>().erase(rhs);
	index.get<overlap_rhs_t>().erase(lhs);
	index.get<overlap_rhs_t>().erase(rhs);
      }

      auto super_begin = std::ranges::begin(superstring);
      auto super_end   = std::ranges::end  (superstring);

      for(auto &&[i, substring] : ::ranges::views::enumerate(range)) {
        auto offset = std::ranges::distance
	  (super_begin, std::search(super_begin, super_end, searchers[i]));

        *out++ = bounds_t<R> { offset, substring.size() };
      }

      return { std::ranges::end(range), out, std::move(superstring), cum_overlap };
    }

  } const shortest_common_superstring { };
}

// clang-format on

#endif
