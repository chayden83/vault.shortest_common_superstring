// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <cstdint>
#include <iterator>
#include <ranges>
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

      struct job_t {
	std::ranges::iterator_t<haystack_t const> haystack_first = { };
	std::ranges::iterator_t<haystack_t const> haystack_last  = { };

	std::ranges::iterator_t<needles_t const> needle_itr = { };
	
	[[nodiscard]] constexpr job_t(haystack_t const &haystack, std::ranges::iterator_t<needles_t const> needle_itr)
	  : haystack_first { std::ranges::begin(haystack) }
	  , haystack_last  { std::ranges::end  (haystack) }
	  , needle_itr     { needle_itr }
	{ }

	[[nodiscard]] void const *first_address() const {
	  if(haystack_first == haystack_last) {
	    return nullptr;
	  }

	  return std::addressof(*bisect(haystack_first, haystack_last));
	}

	[[nodiscard]] void const *step() {
	  auto haystack_middle = bisect(haystack_first, haystack_last);

	  // TODO: Generalize comparison.
	  if(*haystack_middle < *needle_itr) {
	    haystack_first = ++haystack_middle;
	  } else {
	    haystack_last  =   haystack_middle;
	  }

	  return std::addressof(*bisect(haystack_first, haystack_last));
	}
      };

      struct alignas(job_t) job_slot_t {
	std::byte storage[sizeof(job_t)];

	[[nodiscard]] job_t *get() noexcept {
	  return reinterpret_cast<job_t *>(&storage[0]);
	}
      };

      auto next_needle_itr = std::ranges::begin(needles);
      auto next_needle_end = std::ranges::end  (needles);

      auto jobs = std::array<job_slot_t, N> { };

      auto jobs_first = std::ranges::begin(jobs);
      auto jobs_last  = std::ranges::end  (jobs);

      while(jobs_first != jobs_last and next_needle_itr != next_needle_end) {
	new (jobs_first -> get()) job_t { haystack, next_needle_itr++ };

	auto *address = jobs_first -> get() -> first_address();

	while(address == nullptr && next_needle_itr != next_needle_end) {
	  *jobs_first -> get() = job_t { haystack, next_needle_itr++ };
	  address = jobs_first -> get() -> first_address();
	}

	if(address != nullptr) {
	  __builtin_prefetch(address);
	}

	++jobs_first;
      }

      jobs_last = std::exchange(jobs_first, std::ranges::begin(jobs));





      /////////////
      // EXECUTE //
      /////////////

      while(jobs_first != jobs_last) {
	for(auto jobs_itr = jobs_first; jobs_itr != jobs_last; ++jobs_itr) {
	  if(auto *next_address = jobs_itr -> get() -> step()) {
	    __builtin_prefetch(next_address);  continue;	    
	  }

	  std::invoke(report, *jobs_itr -> get());

	  if(next_needle_itr != next_needle_end) {
	    *jobs_itr -> get() = job_t { haystack, next_needle_itr++ };
	  } else {
	    --jobs_last;
	    std::exchange(*jobs_itr -> get(), *jobs_last -> get());
	    jobs_last -> get() -> ~job_t();
	  }

	  if(jobs_itr == jobs_last) {
	    break;
	  } else if(auto *next_address = jobs_itr -> get() -> first_address()) {
	    __builtin_prefetch(next_address);
	  }
	}
      }
    }
  };

  template<uint8_t N>
  constexpr inline auto const amac = amac_fn<N> { };
}

#endif
