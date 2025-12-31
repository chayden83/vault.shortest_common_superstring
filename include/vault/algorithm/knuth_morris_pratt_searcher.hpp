// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP

#include <ranges>
#include <vector>
#include <concepts>
#include <iterator>

// clang-format off

namespace vault::algorithm {
  template<typename T>
  concept knuth_morris_pratt_failure_table = true
    && std::copy_constructible<T>
    && std::ranges::random_access_range<T>
    && std::integral<std::ranges::range_value_t<T>>;

  /**
   * @todo Add overloads that accept an output iterator and/or a sink.
   */
  constexpr inline struct knuth_morris_pratt_failure_function_fn {
    template<std::forward_iterator I, std::sentinel_for<I> S>
      requires std::equality_comparable<std::iter_reference_t<I>>
    [[nodiscard]] static constexpr std::vector<int> operator ()(I first, S last) {
      auto length_of_pattern = std::ranges::distance(first, last);
      auto length_of_previous_longest_prefix = 0;
      
      auto failure_function = std::vector<int>(length_of_pattern, 0);
      
      for(auto i = 1; i < length_of_pattern; ) {
	if(*std::ranges::next(first, i) == *std::ranges::next(first, length_of_previous_longest_prefix)) {
	  failure_function[i++] = ++length_of_previous_longest_prefix;
	} else if(length_of_previous_longest_prefix != 0) {
	  length_of_previous_longest_prefix = failure_function[length_of_previous_longest_prefix - 1];
	} else {
	  failure_function[i++] = 0;
	}
      }
      
      return failure_function;
    }
    
    template<std::ranges::forward_range Pattern>
      requires std::equality_comparable<std::ranges::range_reference_t<Pattern>>
    [[nodiscard]] static constexpr std::vector<int> operator ()(Pattern &&pattern) {
      return operator ()(std::ranges::begin(pattern), std::ranges::end(pattern));
    }
  } const knuth_morris_pratt_failure_function { };

  constexpr inline struct knuth_morris_pratt_overlap_fn {
    template<std::forward_iterator ILHS,
	     std::forward_iterator IRHS,
	     knuth_morris_pratt_failure_table FailureTable = std::vector<int>>
    struct result {
      std::iter_difference_t<IRHS> score;

      ILHS lhs_first;
      ILHS lhs_last;

      IRHS rhs_first;
      IRHS rhs_last;

      FailureTable failure_table;
    };

    template<std::forward_iterator ILHS, std::sentinel_for<ILHS> SLHS,
	     std::forward_iterator IRHS, std::sentinel_for<IRHS> SRHS,
	     knuth_morris_pratt_failure_table FailureTable>
      requires std::equality_comparable_with
	<std::iter_reference_t<ILHS>, std::iter_reference_t<IRHS>>
    [[nodiscard]] static constexpr result<ILHS, IRHS, FailureTable> operator ()
      (ILHS lhs_first, SLHS lhs_last, IRHS rhs_first, SRHS rhs_last, FailureTable &&failure_table)
    {
      auto rhs_index = std::iter_difference_t<IRHS> { 0 };

      auto lhs_length = std::ranges::distance(lhs_first, lhs_last);
      auto rhs_length = std::ranges::distance(rhs_first, rhs_last);

      auto lhs_cursor = lhs_first;
      
      for(lhs_cursor; lhs_cursor != lhs_last; ++lhs_cursor) {
        while (rhs_index > 0) {
	  if(rhs_index != rhs_length && *lhs_cursor == *std::next(rhs_first, rhs_index)) {
	    break;
	  }
	  
	  rhs_index = failure_table[rhs_index - 1];
        }
	
        if (*lhs_cursor == *std::next(rhs_first, rhs_index)) {
	  rhs_index++;
        }
      }

      return {
	rhs_index,
	std::next(lhs_first, lhs_length - rhs_index),
	lhs_cursor,
	rhs_first,
	std::next(rhs_first, rhs_index),
	std::move(failure_table)
      };
    }

    template<std::forward_iterator ILHS, std::sentinel_for<ILHS> SLHS,
	     std::forward_iterator IRHS, std::sentinel_for<IRHS> SRHS>
      requires std::equality_comparable_with
	<std::iter_reference_t<ILHS>, std::iter_reference_t<IRHS>>
    [[nodiscard]] static constexpr result<ILHS, IRHS> operator ()
      (ILHS lhs_first, SLHS lhs_last, IRHS rhs_first, SRHS rhs_last)
    {
      auto failure_table = knuth_morris_pratt_failure_function(rhs_first, rhs_last);

      return operator ()
	(lhs_first, lhs_last, rhs_first, rhs_last, std::move(failure_table));
    }

    template<std::ranges::forward_range LHS,
	     std::ranges::forward_range RHS,
	     knuth_morris_pratt_failure_table FailureTable>
      requires std::equality_comparable_with
	<std::ranges::range_reference_t<LHS>, std::ranges::range_reference_t<RHS>>
    [[nodiscard]] static constexpr auto operator ()(LHS &&lhs, RHS &&rhs, FailureTable &&failure_table) ->
      result<std::ranges::iterator_t<LHS>, std::ranges::iterator_t<RHS>, FailureTable>
    {
      return operator ()(std::ranges::begin(lhs), std::ranges::end(lhs),
			 std::ranges::begin(rhs), std::ranges::end(rhs),
			 std::forward<FailureTable>(failure_table));
    }    

    template<std::ranges::forward_range LHS, std::ranges::forward_range RHS>
      requires std::equality_comparable_with
	<std::ranges::range_reference_t<LHS>, std::ranges::range_reference_t<RHS>>
    [[nodiscard]] static constexpr auto operator ()(LHS &&lhs, RHS &&rhs) ->
      result<std::ranges::iterator_t<LHS>, std::ranges::iterator_t<RHS>>
    {
      return operator ()(std::ranges::begin(lhs), std::ranges::end(lhs),
			 std::ranges::begin(rhs), std::ranges::end(rhs));
    }    
  } const knuth_morris_pratt_overlap { };

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
      
      auto pattern_index = 0;

      auto const pattern_first = std::ranges::begin(m_pattern);
      auto const pattern_last  = std::ranges::end  (m_pattern);

      auto const pattern_length = std::ranges::distance
	(pattern_first, pattern_last);

      for(auto current = first; current != last; ++current) {
	// Backtrack until we find a match.
	while (pattern_index > 0 && *current != *std::next(pattern_first, pattern_index)) {
	  pattern_index = m_failure_table[pattern_index - 1];
	}
	
	// If match found, increment pattern index.
	if(*current == *std::next(pattern_first, pattern_index)) {
	  pattern_index++;
	}
	
	// Check if complete pattern has been matched.
	if(pattern_index == pattern_length) {
	  auto match_first = std::ranges::next
	    (first, std::ranges::distance(first, current) - pattern_length + 1);

	  return { std::move(match_first), std::ranges::next(current) };
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
}

// clang-format on

#endif
