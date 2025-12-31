// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP

#include <ranges>
#include <vector>
#include <concepts>
#include <iterator>

// clang-format off

namespace vault::algorithm {
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

  template <std::ranges::random_access_range Pattern>
    requires std::equality_comparable<std::ranges::range_reference_t<Pattern>>
  class knuth_morris_pratt_searcher {
    Pattern m_pattern;
    std::vector<int> m_failure_function;

  public:
    [[nodiscard]] constexpr knuth_morris_pratt_searcher(Pattern pattern, std::vector<int> failure_function)
      : m_pattern { std::move(pattern) }
      , m_failure_function { std::move(failure_function) }
    { }

    [[nodiscard]] constexpr knuth_morris_pratt_searcher(Pattern pattern)
      : m_pattern { std::move(pattern) }
      , m_failure_function { knuth_morris_pratt_failure_function(m_pattern) }
    { }

    template<std::forward_iterator I, std::sentinel_for<I> S>
      requires std::equality_comparable_with<
	std::iter_reference_t<I>,
	std::ranges::range_reference_t<Pattern>
      >
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
	  pattern_index = m_failure_function[pattern_index - 1];
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
      requires std::equality_comparable_with<
	std::ranges::range_reference_t<Data>,
	std::ranges::range_reference_t<Pattern>
      >
    [[nodiscard]] constexpr
      std::pair<std::ranges::iterator_t<Data>, std::ranges::iterator_t<Data>>
    operator ()(Data &&data) const {
      return operator ()(std::ranges::begin(data), std::ranges::end(data));
    }
  };

  template<std::ranges::forward_range Pattern>
    requires std::equality_comparable<std::ranges::range_reference_t<Pattern>>
  knuth_morris_pratt_searcher(Pattern &&pattern) ->
    knuth_morris_pratt_searcher<std::remove_cvref_t<Pattern>>;
}

// clang-format on

#endif
