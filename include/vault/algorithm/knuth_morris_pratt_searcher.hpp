// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP

#include <concepts>
#include <iterator>
#include <ranges>
#include <vector>

#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>

// clang-format off

namespace vault::algorithm {
  template <std::ranges::random_access_range Pattern,
	    knuth_morris_pratt_failure_table FailureTable = std::vector<int>>
    requires std::equality_comparable<std::ranges::range_reference_t<Pattern>>
  class knuth_morris_pratt_searcher {
    Pattern      m_pattern;
    FailureTable m_failure_table;

  public:
    [[nodiscard]] constexpr knuth_morris_pratt_searcher(Pattern pattern, FailureTable failure_table)
      : m_pattern { std::move(pattern) }
      , m_failure_table { std::move(failure_table) }
    { }

    [[nodiscard]] constexpr knuth_morris_pratt_searcher(Pattern pattern)
      : m_pattern { std::move(pattern) }
      , m_failure_table { knuth_morris_pratt_failure_function(m_pattern) }
    { }

    template<std::forward_iterator I, std::sentinel_for<I> S>
      requires std::equality_comparable_with
	<std::iter_reference_t<I>, std::ranges::range_reference_t<Pattern>>
    [[nodiscard]] constexpr std::pair<I, I> operator ()(I first, S last) const {
      if(std::ranges::empty(m_pattern)) {
	return { first, first };
      }
      
      auto pattern_index  = 0;
      auto pattern_first  = std::ranges::begin(m_pattern);
      auto pattern_cursor = std::next(pattern_first, pattern_index);

      auto pattern_length = std::ranges::distance
	(pattern_first, std::ranges::end(m_pattern));

      for(auto cursor = first; cursor != last; ++cursor) {
	// Backtrack until we find a match.
	while (pattern_index > 0 && *cursor != *pattern_cursor) {
	  pattern_index  = m_failure_table[pattern_index - 1];
	  pattern_cursor = std::next(pattern_first, pattern_index);
	}
	
	// If match found, move to the next "character" of the
	// pattern.
	if(*cursor == *pattern_cursor) {
	  ++pattern_index;
	  ++pattern_cursor;
	}
	
	// We are done if the complete pattern has been matched.
	if(pattern_index == pattern_length) {
	  auto match_first = std::ranges::next
	    (first, std::ranges::distance(first, cursor) - pattern_length + 1);

	  return { std::move(match_first), std::ranges::next(cursor) };
	}
      }
      
      return { last, last };
    }

    template<std::ranges::forward_range Data>
      requires std::equality_comparable_with
	<std::ranges::range_reference_t<Data>, std::ranges::range_reference_t<Pattern>>
    [[nodiscard]] constexpr
      std::pair<std::ranges::iterator_t<Data>, std::ranges::iterator_t<Data>>
    operator ()(Data &&data) const {
      return operator ()(std::ranges::begin(data), std::ranges::end(data));
    }
  };

  template<std::ranges::forward_range Pattern,
	   knuth_morris_pratt_failure_table FailureTable>
    requires std::equality_comparable<std::ranges::range_reference_t<Pattern>>
  knuth_morris_pratt_searcher(Pattern &&, FailureTable &&) ->
    knuth_morris_pratt_searcher<std::remove_cvref_t<Pattern>, std::remove_cvref_t<FailureTable>>;

  template<std::ranges::forward_range Pattern>
    requires std::equality_comparable<std::ranges::range_reference_t<Pattern>>
  knuth_morris_pratt_searcher(Pattern &&) ->
    knuth_morris_pratt_searcher<std::remove_cvref_t<Pattern>>;

  constexpr inline struct make_knuth_morris_pratt_searcher_fn {
    [[nodiscard]] static constexpr auto operator ()(auto&&... args) ->
      decltype(knuth_morris_pratt_searcher { std::forward<decltype(args)>(args)... })
    {
      return knuth_morris_pratt_searcher { std::forward<decltype(args)>(args)... };
    }
  } const make_knuth_morris_pratt_searcher { };
}

// clang-format on

#endif
