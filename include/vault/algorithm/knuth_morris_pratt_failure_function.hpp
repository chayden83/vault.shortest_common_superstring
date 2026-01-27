// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_FAILURE_FUNCTION_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_FAILURE_FUNCTION_HPP

#include <concepts>
#include <iterator>
#include <ranges>
#include <vector>

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
}

// clang-format on

#endif
