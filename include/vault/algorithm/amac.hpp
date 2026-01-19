// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <cstdint>
#include <iterator>
#include <utility>
#include <functional>

// clang-format off

namespace vault::algorithm {
  template<std::forward_iterator I, std::sentinel_for<I> S>
  [[nodiscard]] constexpr I bisect(I first, S last) {
    return std::ranges::next(first, std::ranges::distance(first, last) / 2);
  }

  template<uint8_t N>
  struct amac_fn {
    template<typename haystack_t, typename needles_t, typename comp_t = std::less<>>
    static constexpr void operator ()
      (haystack_t const &haystack, needles_t const &needles, auto report, comp_t comp = {})
    {
      ///////////
      // SETUP //
      ///////////

      struct job_state_t {
	std::size_t i_needle = 0uz;

	std::ranges::iterator_t<haystack_t const> haystack_first = { };
	std::ranges::iterator_t<haystack_t const> haystack_last  = { };
      };

      auto jobs = std::invoke([&]<std::size_t... I>(std::index_sequence<I...>) {
	return std::array<job_state_t, N> {
	  job_state_t { I, std::ranges::begin(haystack), std::ranges::end(haystack) } ...
	};
      }, std::make_index_sequence<N> { });

      auto i_next_needle = std::min(std::ranges::size(needles), std::size_t { N });

      /////////////
      // EXECUTE //
      /////////////

      auto jobs_first = std::ranges::begin(jobs);

      auto jobs_last  = std::ranges::next
	(jobs_first, std::min(std::ranges::size(needles), std::size_t { N }));

      while(jobs_first != jobs_last) {
	auto jobs_itr = jobs_first;

	while(jobs_itr != jobs_last) {
	  ///////////////////////////////////////
	  // REPORT RESULT AND PULL IN NEW JOB //
	  ///////////////////////////////////////
	  if(jobs_itr -> haystack_first == jobs_itr -> haystack_last) {
	    std::invoke(report, jobs_itr -> i_needle, jobs_itr -> haystack_last);

	    if(i_next_needle < std::ranges::size(needles)) {
	      jobs_itr -> i_needle = i_next_needle++;

	      jobs_itr -> haystack_first = std::ranges::begin(haystack);
	      jobs_itr -> haystack_last  = std::ranges::end  (haystack);
	    } else {
	      *jobs_itr = std::move(*--jobs_last);
	    }

	    continue;
	  }

	  /////////////////////////////////////
	  // UPDATE AND CONTINUE TO NEXT JOB //
	  /////////////////////////////////////
	  auto haystack_middle = bisect
	    (jobs_itr -> haystack_first, jobs_itr -> haystack_last);

	  if(std::invoke(comp, *haystack_middle, needles[jobs_itr -> i_needle])) {
	    jobs_itr -> haystack_first = ++haystack_middle;
	  } else {
	    jobs_itr -> haystack_last  =   haystack_middle;
	  }

	  __builtin_prefetch(std::addressof(*bisect(
	    jobs_itr -> haystack_first, jobs_itr -> haystack_last
	  )));

	  ++jobs_itr;
	}
      }
    }
  };

  template<uint8_t N>
  constexpr inline auto const amac = amac_fn<N> { };
}

#endif
