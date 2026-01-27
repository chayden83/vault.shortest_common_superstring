// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP
#define VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_HPP

#include <print>

#include <algorithm>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

#include <boost/multi_index_container.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <range/v3/range/conversion.hpp>

#include <range/v3/view/addressof.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>

#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>
#include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
#include <vault/algorithm/knuth_morris_pratt_searcher.hpp>

// clang-format off

namespace vault::algorithm {
  constexpr inline struct shortest_common_superstring_fn {
    static constexpr inline auto const strlen_fn = []<std::ranges::range R>(R const &range) {
      if constexpr (std::ranges::sized_range<R>) {
	return std::ranges::size(range);
      } else {
	return std::ranges::distance(std::ranges::begin(range), std::ranges::end(range));
      }
    };

    struct index_entry_t {
      uint32_t lhs = 0;
      uint32_t rhs = 0;

      int score = 0;
    };

    struct index_lhs_t { };
    struct index_rhs_t { };

    struct index_score_t { };

    using index_t = boost::multi_index_container<
      index_entry_t, boost::multi_index::indexed_by<
        boost::multi_index::ordered_non_unique<
	  boost::multi_index::tag<index_score_t>,
	  boost::multi_index::member<index_entry_t, int, &index_entry_t::score>,
	  std::greater<>
	>,
        boost::multi_index::hashed_non_unique<
	  boost::multi_index::tag<index_lhs_t>,
	  boost::multi_index::member<index_entry_t, uint32_t, &index_entry_t::lhs>
	>,
        boost::multi_index::hashed_non_unique<
	  boost::multi_index::tag<index_rhs_t>,
	  boost::multi_index::member<index_entry_t, uint32_t, &index_entry_t::rhs>
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
    static auto operator ()(R &&strings, O out) ->
      result<std::ranges::iterator_t<R>, O, std::ranges::range_value_t<R>>
    {
      if(std::ranges::empty(strings)) {
	return { std::ranges::end(strings), out, {}, 0 };
      }

      if(std::ranges::next(std::ranges::begin(strings)) == std::ranges::end(strings)) {
	*out++ = bounds_t<R> { 0, strlen_fn(*std::ranges::begin(strings)) };
	return { std::ranges::end(strings), out, *std::ranges::begin(strings), 0 };
      }

      auto total_overlap = 0;

      auto ftables = strings
	| ::ranges::views::transform(knuth_morris_pratt_failure_function)
	| ::ranges::to<std::vector>();

      auto string_ptrs = ::ranges::to<std::vector>(::ranges::views::addressof(strings));
      auto ftable_ptrs = ::ranges::to<std::vector>(::ranges::views::addressof(ftables));

      std::invoke([&] {
	std::ranges::sort(::ranges::views::zip(string_ptrs, ftable_ptrs), {}, [](auto ptr_pair) {
	  return strlen_fn(*ptr_pair.first);
	});

	auto out_cursor_strings = std::ranges::begin(string_ptrs);
	auto out_cursor_ftables = std::ranges::begin(ftable_ptrs);

	auto in_cursor_strings = std::ranges::begin(string_ptrs);
	auto in_cursor_ftables = std::ranges::begin(ftable_ptrs);

	while(in_cursor_strings != std::ranges::end(string_ptrs)) {
	  auto searcher = knuth_morris_pratt_searcher { **in_cursor_strings, **in_cursor_ftables };

	  auto is_superstring = [searcher = std::move(searcher)](auto *haystrand_ptr) {
	    return std::search(std::ranges::begin(*haystrand_ptr), std::ranges::end(*haystrand_ptr), searcher) != std::ranges::end(*haystrand_ptr);
	  };

	  if(std::ranges::none_of(std::ranges::next(in_cursor_strings), std::ranges::end(string_ptrs), is_superstring)) {
	    *out_cursor_strings++ = *in_cursor_strings;
	    *out_cursor_ftables++ = *in_cursor_ftables;
	  } else {
	    total_overlap += strlen_fn(**in_cursor_strings);
	  }

	  ++in_cursor_strings;
	  ++in_cursor_ftables;
	}

	string_ptrs.erase(out_cursor_strings, std::ranges::end(string_ptrs));
	ftable_ptrs.erase(out_cursor_ftables, std::ranges::end(ftable_ptrs));
      });

      auto reduced_strings = ::ranges::to<std::vector>
	(::ranges::views::indirect(string_ptrs));

      auto index = index_t { };

      for(auto i = 0u; i < reduced_strings.size(); ++i) {
	for(auto j = 0u; j < reduced_strings.size(); ++j) {
	  if(i == j) continue;

	  auto score = knuth_morris_pratt_overlap
	    (reduced_strings[i], reduced_strings[j], *ftable_ptrs[j]).score;

	  index.emplace(i, j, score);
	}
      }

      auto *superstring = static_cast<std::string const *>(nullptr);

      while(index.size() != 0) {
	auto const [lhs, rhs, overlap] = index.get<index_score_t>()
	  .extract(index.get<index_score_t>().begin()).value();

	reduced_strings[lhs].append(reduced_strings[rhs], overlap);

	index.get<index_lhs_t>().erase(lhs);
	index.get<index_rhs_t>().erase(rhs);
	
	for(auto [first, last] = index.get<index_lhs_t>().equal_range(rhs); first != last; ++first) {
	  if(first -> rhs != lhs) index.emplace(lhs, first -> rhs, first -> score);
	}

	index.get<index_lhs_t>().erase(rhs);

	total_overlap += overlap;
	superstring = std::addressof(reduced_strings[lhs]);
      }

      auto super_begin = std::ranges::begin(*superstring);
      auto super_end   = std::ranges::end  (*superstring);

      for(auto &&[substring, ftable] : ::ranges::views::zip(strings, ftables)) {
	auto searcher = knuth_morris_pratt_searcher { substring, std::move(ftable) };

        auto offset = std::ranges::distance
	  (super_begin, std::search(super_begin, super_end, searcher));

        *out++ = bounds_t<R> { offset, substring.size() };
      }

      return { std::ranges::end(strings), out, std::move(*superstring), total_overlap };
    }
  } const shortest_common_superstring { };
}

// clang-format on

#endif
