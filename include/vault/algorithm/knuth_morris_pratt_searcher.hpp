// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP
#define VAULT_ALGORITHM_KNUTH_MORRIS_PRATT_SEARCHER_HPP

#include <iterator>
#include <ranges>
#include <vector>

// clang-format off

namespace vault::algorithm {
  template<std::forward_iterator I, std::sentinel_for<I> S>
  [[nodiscard]] constexpr std::vector<int> knuth_morris_pratt_failure_function(I first, S last) {
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
  [[nodiscard]] constexpr std::vector<int> knuth_morris_pratt_failure_function(Pattern &&pattern) {
    return knuth_morris_pratt_failure_function(std::ranges::begin(pattern), std::ranges::end(pattern));
  }

  template <std::ranges::random_access_range Pattern>
    requires std::ranges::sized_range<Pattern>
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
    [[nodiscard]] constexpr std::pair<I, I> operator ()(I first, S last) const {
      return { }; // TODO
    }
  };

  template<std::ranges::random_access_range Pattern>
  knuth_morris_pratt_searcher(Pattern &&pattern) ->
    knuth_morris_pratt_searcher<std::remove_cvref_t<Pattern>>;
}

// clang-format on

#endif
