// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <ranges>
#include <cstdint>
#include <utility>
#include <iterator>
#include <algorithm>
#include <functional>

// clang-format off

namespace vault::algorithm {
  template<typename T, typename U>
  constexpr T &assign(T &lhs, U &&rhs) {
    lhs = std::forward<U>(rhs);  return lhs;
  }

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

	  return first_address();
	}
      };

      struct alignas(job_t) job_slot_t {
	std::byte storage[sizeof(job_t)];

	[[nodiscard]] job_t *get() noexcept {
	  return reinterpret_cast<job_t *>(&storage[0]);
	}
      };

      auto needles_itr = std::ranges::begin(needles);
      auto needles_end = std::ranges::end  (needles);

      auto jobs = std::array<job_slot_t, N> { };

      auto jobs_first = std::ranges::begin(jobs);
      auto jobs_last  = std::ranges::end  (jobs);

      while(jobs_first != jobs_last and needles_itr != needles_end) {
	auto *job = std::construct_at
	  (jobs_first -> get(), haystack, needles_itr++);

	auto *address = job -> first_address();

	while(address == nullptr && needles_itr != needles_end) {
	  std::invoke(report, *job);

	  address = assign(*job, job_t { haystack, needles_itr++ })
	    .step();
	}

	if(address != nullptr) {
	  __builtin_prefetch(address);
	}

	++jobs_first;
      }

      jobs_last = std::exchange(jobs_first, std::ranges::begin(jobs));

      if(jobs_first == jobs_last) {
	return;
      }

      /////////////
      // EXECUTE //
      /////////////

      auto active_jobs_first = jobs_first;
      auto active_jobs_last  = jobs_last;

      while(true) {
	for(/*noop*/; active_jobs_first != active_jobs_last; ++active_jobs_first) {
	  auto *next_address = active_jobs_first -> get() -> step();

	  while(next_address == nullptr) {
	    std::invoke(report, *active_jobs_first -> get());

	    if(needles_itr == needles_end) {
	      goto needles_consumed;
	    }

	    auto &job = assign
	      (*active_jobs_first -> get(), job_t { haystack, needles_itr++ });

	    next_address = job.first_address();
	  }

	  __builtin_prefetch(next_address);
	}

	active_jobs_first = jobs_first;
      }

    needles_consumed:

      // The loop above breaks when it encounters a finished job for
      // which there is no remaining needle with which it can
      // construct a new job. That means the active_jobs_first
      // iterator refers to a now inactive job, so we shift it out of
      // the array.
      active_jobs_last = std::shift_left
	(active_jobs_first, active_jobs_last, 1);

      auto still_active = [&](auto &job) {
	if(auto *next_address = job.get() -> step()) {
	  return __builtin_prefetch(next_address), true;
	} else {
	  return std::invoke(report, *job.get()), false;
	}
      };

      // We use stable partition to remove the jobs that are finished
      // while preservingt the order of the remaining jobs. This
      // maximizes the latency between consecutive steps on the same
      // job in order to give the the prefetch instruction the
      // greatest possible opportunity to complete.
      active_jobs_last = std::stable_partition
	(active_jobs_first, active_jobs_last, still_active);

      while(active_jobs_last != jobs_first) {
	active_jobs_last = std::stable_partition
	  (jobs_first, active_jobs_last, still_active);
      }

      // We constructed the jobs in-place, so we have to explicitly
      // destroy them.
      std::destroy(jobs_first, jobs_last);
    }
  };

  template<uint8_t N>
  constexpr inline auto const amac = amac_fn<N> { };
}

#endif
