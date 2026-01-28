// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <iterator>
#include <ranges>
#include <tuple>
#include <utility>

namespace vault::amac::concepts {
  template <typename T>
  concept job_step_result = std::constructible_from<bool, T> &&
    []<std::size_t... Is>(std::index_sequence<Is...>) {
      return (std::same_as<void const*, std::tuple_element_t<Is, T>> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});

  template <typename J, typename R, typename I>
  concept job = std::ranges::range<R> && std::input_iterator<I>
    && std::move_constructible<J> && requires(J& job) {
         { job.init() } -> job_step_result;
         { job.step() } -> job_step_result;
       };

  template <typename F, typename R, typename I>
  concept job_factory = job<std::invoke_result_t<F, R const&, I>, R, I>;

  template <typename R, typename J>
  concept job_reporter = std::invocable<R, J&&>;
} // namespace vault::amac::concepts

namespace vault::amac {
  template <std::size_t N>
  struct job_step_result : public std::array<void const*, N> {
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
      return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((*this)[Is] == nullptr) && ...);
      }(std::make_index_sequence<N>{});
    }
  };

  template <uint8_t N> class coordinator_fn {
    template <concepts::job_step_result J>
    static constexpr void prefetch(J const& step_result)
    {
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (__builtin_prefetch(std::get<Is>(step_result), 0, 3), ...);
      }(std::make_index_sequence<std::tuple_size_v<J>>{});
    }

  public:
    template <std::ranges::forward_range           haystack_t,
      std::ranges::input_range                     needles_t,
      concepts::job_factory<haystack_t,
        std::ranges::iterator_t<needles_t const>>  job_factory_t,
      concepts::job_reporter<std::invoke_result_t<job_factory_t,
        haystack_t const&,
        std::ranges::iterator_t<needles_t const>>> reporter_t>
    static constexpr void operator()(haystack_t const& haystack,
      needles_t const&                                 needles,
      job_factory_t                                    job_factory,
      reporter_t                                       reporter)
    {
      ///////////
      // SETUP //
      ///////////
      using job_t = decltype(std::invoke(
        job_factory, haystack, std::ranges::begin(needles)));

      struct alignas(job_t) job_slot_t {
        std::byte storage[sizeof(job_t)];

        [[nodiscard]] job_t* get() noexcept
        {
          return reinterpret_cast<job_t*>(&storage[0]);
        }
      };

      auto jobs = std::array<job_slot_t, N>{};

      auto needles_itr = std::ranges::begin(needles);
      auto needles_end = std::ranges::end(needles);

      auto const [jobs_first, jobs_last] = std::invoke([&] {
        auto jobs_first = std::ranges::begin(jobs);
        auto jobs_last  = std::ranges::end(jobs);

        while (jobs_first != jobs_last and needles_itr != needles_end) {
          auto job = job_factory(haystack, needles_itr++);

          if (auto addresses = job.init()) {
            prefetch(addresses);
            std::construct_at(jobs_first->get(), std::move(job));
            ++jobs_first;
          }
        }

        return std::pair{std::ranges::begin(jobs), jobs_first};
      });

      /////////////
      // EXECUTE //
      /////////////

      auto active_jobs_first = jobs_first;
      auto active_jobs_last  = jobs_last;

      auto is_active = [&](auto& job) {
        if (auto addresses = job.get()->step()) {
          return prefetch(addresses), true;
        } else {
          return std::invoke(reporter, std::move(*job.get())), false;
        }
      };

      // Step each of the active jobs one after another. If a job
      // completes, we begin constructing new jobs from the remaining
      // needles. Once we find a newly constructed job that
      // successfully activates, we insert it into the jobs slot where
      // we found the complete job. We immediately report and then
      // discard any newly constructed job that fails to activate.
      while (needles_itr != needles_end) {
        active_jobs_first =
          std::find_if_not(active_jobs_first, active_jobs_last, is_active);

        if (active_jobs_first == active_jobs_last) {
          active_jobs_first = jobs_first;
          continue;
        };

        while (needles_itr != needles_end) {
          auto job = job_factory(haystack, needles_itr++);

          if (auto addresses = job.init()) {
            prefetch(addresses);
            *active_jobs_first->get() = std::move(job);
            ++active_jobs_first;
            break;
          }

          std::invoke(reporter, std::move(job));
        }
      }

      // Remove the jobs that are finished while preserving the order
      // of the remaining jobs. This maximizes the latency between
      // consecutive steps on the same job in order to give the the
      // prefetch instruction the greatest possible opportunity to
      // complete.
      active_jobs_last = std::remove_if(
        active_jobs_first, active_jobs_last, std::not_fn(is_active));

      while (active_jobs_last != jobs_first) {
        active_jobs_last =
          std::remove_if(jobs_first, active_jobs_last, std::not_fn(is_active));
      }

      // We constructed the jobs in-place, so we have to explicitly
      // destroy them.
      std::destroy(jobs_first, jobs_last);
    }
  };

  template <uint8_t N>
  constexpr inline auto const coordinator = coordinator_fn<N>{};
} // namespace vault::amac

namespace vault::amac {
  template <uint8_t N> struct lower_bound_fn {
    template <typename haystack_t, typename needles_t> class job_t {
      std::ranges::iterator_t<haystack_t const> m_haystack_first = {};
      std::ranges::iterator_t<haystack_t const> m_haystack_last  = {};

      std::ranges::iterator_t<needles_t const> m_needle_itr = {};

      template <std::forward_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr I bisect(I first, S last)
      {
        return std::ranges::next(first, std::ranges::distance(first, last) / 2);
      }

    public:
      [[nodiscard]] constexpr job_t(haystack_t const& haystack,
        std::ranges::iterator_t<needles_t const>      needle_itr)
          : m_haystack_first{std::ranges::begin(haystack)}
          , m_haystack_last{std::ranges::end(haystack)}
          , m_needle_itr{needle_itr}
      {}

      [[nodiscard]] job_step_result<1> init() const
      {
        if (m_haystack_first == m_haystack_last) {
          return {nullptr};
        }

        return {std::addressof(*bisect(m_haystack_first, m_haystack_last))};
      }

      [[nodiscard]] job_step_result<1> step()
      {
        auto haystack_middle = bisect(m_haystack_first, m_haystack_last);

        // TODO: Generalize comparison.
        if (*haystack_middle < *m_needle_itr) {
          m_haystack_first = ++haystack_middle;
        } else {
          m_haystack_last = haystack_middle;
        }

        return init();
      }

      [[nodiscard]] std::ranges::iterator_t<needles_t const> needle_itr() const
      {
        return m_needle_itr;
      }

      [[nodiscard]] std::ranges::iterator_t<haystack_t const>
      haystack_first() const
      {
        return m_haystack_first;
      }

      [[nodiscard]] std::ranges::iterator_t<haystack_t const>
      haystack_last() const
      {
        return m_haystack_last;
      }
    };

    template <typename haystack_t, typename needles_t>
    static constexpr void operator()(
      haystack_t const& haystack, needles_t const& needles, auto reporter)
    {
      auto job_factory =
        [](haystack_t const&                       haystack,
          std::ranges::iterator_t<needles_t const> needle_itr) {
          return job_t<haystack_t, needles_t>(haystack, needle_itr);
        };

      coordinator<N>(haystack, needles, job_factory, std::move(reporter));
    }
  };

  template <uint8_t N>
  constexpr inline auto const lower_bound = lower_bound_fn<N>{};
} // namespace vault::amac

template <std::size_t N>
struct std::tuple_size<vault::amac::job_step_result<N>> {
  static constexpr inline auto const value = std::size_t{N};
};

template <std::size_t I, std::size_t N>
struct std::tuple_element<I, vault::amac::job_step_result<N>> {
  using type = void const*;
};

#endif
