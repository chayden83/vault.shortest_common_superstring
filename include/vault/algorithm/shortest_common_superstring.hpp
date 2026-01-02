// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
#define VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP

#include <span>
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

    struct overlap_t {
      std::string lhs = {};
      std::string rhs = {};

      int score = 0;
    };

    struct overlap_lhs_t { };
    struct overlap_rhs_t { };

    struct overlap_score_t { };

    using overlap_index_t = boost::multi_index_container<
      overlap_t, boost::multi_index::indexed_by<
        boost::multi_index::ordered_non_unique<
	  boost::multi_index::tag<overlap_score_t>,
	  boost::multi_index::member<overlap_t, int, &overlap_t::score>,
	  std::greater<>
	>,
        boost::multi_index::hashed_non_unique<
	  boost::multi_index::tag<overlap_lhs_t>,
	  boost::multi_index::member<overlap_t, std::string, &overlap_t::lhs>
	>,
        boost::multi_index::hashed_non_unique<
	  boost::multi_index::tag<overlap_rhs_t>,
	  boost::multi_index::member<overlap_t, std::string, &overlap_t::rhs>
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
      std::ranges::sort(range, {}, std::ranges::size);

      auto failure_tables = range
	| ::ranges::views::transform(knuth_morris_pratt_failure_function)
	| ::ranges::to<std::vector>();

      auto failure_table_views = failure_tables
	| ::ranges::views::transform([](auto const &table) { return std::span { table }; });

      auto to_pattern_failure_table_pair = [](auto &&parameters) {
	auto &&[pattern, _, failure_table] = parameters;

	return std::pair {
	  std::forward<decltype(pattern      )>(pattern      ),
	  std::forward<decltype(failure_table)>(failure_table)
	};
      };

      auto filtered = ::ranges::views::zip(range, proper_suffixes(range), failure_table_views)
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

      while(index.size() != 0) {
        auto [lhs, rhs, overlap] = index.get<overlap_score_t>().extract
	  (index.get<overlap_score_t>().begin()).value();

	cum_overlap += overlap;
        superstring  = lhs + rhs.substr(overlap);

        for(auto [first, last] = index.get<overlap_lhs_t>().equal_range(lhs); first != last; ++first) {
	  index.emplace(superstring, first -> rhs, knuth_morris_pratt_overlap(superstring, first -> rhs).score);
        }

        for(auto [first, last] = index.get<overlap_rhs_t>().equal_range(rhs); first != last; ++first) {
	  index.emplace(first -> lhs, superstring, knuth_morris_pratt_overlap(first -> lhs, superstring).score);
        }

	index.get<overlap_lhs_t>().erase(lhs);
	index.get<overlap_lhs_t>().erase(rhs);
	index.get<overlap_rhs_t>().erase(lhs);
	index.get<overlap_rhs_t>().erase(rhs);
      }

      for(auto &&substring : range) {
        // TODO: Use the pre-computed KMP failure function to perform
        // the search. We are already computing the KMP failure
        // function to calculate the pairwise string overlaps, so we
        // might as well save some work.
        auto offset = std::ranges::distance
	  (std::ranges::begin(superstring), std::ranges::begin(std::ranges::search(superstring, substring)));

        *out++ = bounds_t<R> { offset, substring.size() };
      }

      return { std::ranges::end(range), out, std::move(superstring), cum_overlap };
    }

  } const shortest_common_superstring { };
}

// clang-format on

#endif
