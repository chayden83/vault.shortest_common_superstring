#ifndef SORTED_LAYOUT_POLICY_HPP
#define SORTED_LAYOUT_POLICY_HPP

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <vault/algorithm/amac.hpp>

namespace eytzinger {

  template <std::size_t Arity = 2> struct sorted_layout_policy {
    static constexpr inline auto const ARITY  = Arity;
    static constexpr inline auto const FANOUT = ARITY - 1;

    /**
     * @brief Unique identifier of verison 1 of the
     * sorted_layout_policy.
     *
     * The version identifier **should** be included in the serialized
     * representation of the layout policy, and it **must** be updated
     * whenever you modify the sorted_layout_policy in a non-backward
     * compatible manner. Otherwise we may deserialize an old version
     * of the layout that is physically compatible with the current
     * version, but logically incompatible. That may result in
     * undefined behavior.
     */
    static constexpr inline auto const UID_V001 = 4185834535822629149uLL;

    template <typename I> struct is_compatible_key_iterator {
      static constexpr bool value = std::random_access_iterator<I>;
    };

    struct sorted_rank_to_index_fn {
      [[nodiscard]] static constexpr std::size_t operator()(
        std::size_t rank, std::size_t n) noexcept
      {
        assert(rank < n && "Rank out of bounds");
        return rank;
      }
    };

    struct index_to_sorted_rank_fn {
      [[nodiscard]] static constexpr std::size_t operator()(
        std::size_t i, std::size_t n) noexcept
      {
        assert(i < n && "Index out of bounds");
        return i;
      }
    };

    static constexpr inline sorted_rank_to_index_fn sorted_rank_to_index{};
    static constexpr inline index_to_sorted_rank_fn index_to_sorted_rank{};

    struct permute_fn {
      template <typename... Args>
      static constexpr void operator()(Args&&...) noexcept
      {}
    };

    struct get_nth_sorted_fn {
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr std::iter_reference_t<I> operator()(
        I first, S last, std::size_t n)
      {
        const auto size = static_cast<std::size_t>(std::distance(first, last));
        if (n >= size) {
          throw std::out_of_range("sorted_layout_policy index out of range");
        }
        assert(n < size);
        return *(first + n);
      }

      template <std::ranges::random_access_range R>
      [[nodiscard]] static constexpr std::ranges::range_reference_t<R>
      operator()(R&& range, std::size_t n)
      {
        return operator()(
          std::ranges::begin(range), std::ranges::end(range), n);
      }
    };

    struct next_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t operator()(
        std::ptrdiff_t i, std::size_t n_sz) noexcept
      {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        // Valid to increment: [0, n-1].
        // Sentinel 'n' cannot be incremented.
        assert(i >= 0 && i < n && "Cannot increment end iterator");
        return i + 1;
      }
    };

    struct prev_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t operator()(
        std::ptrdiff_t i, std::size_t n_sz) noexcept
      {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        assert(i >= 0 && i <= n && "Index out of bounds");

        if (i == n) {
          return (n == 0) ? n : n - 1; // Decrementing end() -> last element
        } else if (i == 0) {
          return n; // Decrementing begin() -> underflow to sentinel
        } else {
          return i - 1;
        }
      }
    };

    struct lower_bound_fn {
      template <std::random_access_iterator I,
        std::sentinel_for<I>                S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr I operator()(
        I first, S last, const T& value, Comp comp = {}, Proj proj = {})
      {
        return std::ranges::lower_bound(first, last, value, comp, proj);
      }

      template <std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R> operator()(
        R&& range, const T& value, Comp comp = {}, Proj proj = {})
      {
        return std::ranges::lower_bound(range, value, comp, proj);
      }
    };

    struct upper_bound_fn {
      template <std::random_access_iterator I,
        std::sentinel_for<I>                S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr I operator()(
        I first, S last, const T& value, Comp comp = {}, Proj proj = {})
      {
        return std::ranges::upper_bound(first, last, value, comp, proj);
      }

      template <std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R> operator()(
        R&& range, const T& value, Comp comp = {}, Proj proj = {})
      {
        return std::ranges::upper_bound(range, value, comp, proj);
      }
    };

    static constexpr inline permute_fn        permute{};
    static constexpr inline get_nth_sorted_fn get_nth_sorted{};
    static constexpr inline next_index_fn     next_index{};
    static constexpr inline prev_index_fn     prev_index{};
    static constexpr inline lower_bound_fn    lower_bound{};
    static constexpr inline upper_bound_fn    upper_bound{};

    // --- Batch Support ---

    template <typename HaystackI, typename NeedleI, typename Compare>
    struct kary_search_job {
      [[nodiscard]] static constexpr uint64_t fanout()
      {
        return sorted_layout_policy::FANOUT;
      }

      NeedleI needle_cursor_;

      HaystackI haystack_cursor_;
      HaystackI haystack_last_;

      [[no_unique_address]] Compare compare_;

      template <std::forward_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr std::array<I, FANOUT> kary_pivots(
        I first, S last)
      {
        auto chunk_size = std::distance(first, last) / ARITY;

        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
          return std::array<I, FANOUT>{
            std::next(first, chunk_size * (Is + 1))...};
        }(std::make_index_sequence<FANOUT>{});
      }

      [[nodiscard]] constexpr vault::amac::job_step_result<FANOUT> init()
      {
        using result = vault::amac::job_step_result<FANOUT>;

        if (haystack_cursor_ == haystack_last_) {
          return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return result{((void)Is, nullptr)...};
          }(std::make_index_sequence<FANOUT>{});
        } else {
          auto pivots = kary_pivots(haystack_cursor_, haystack_last_);

          return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return result{std::addressof(*std::get<Is>(pivots))...};
          }(std::make_index_sequence<FANOUT>{});
        }
      }

      [[nodiscard]] constexpr vault::amac::job_step_result<FANOUT> step()
      {
        auto pivots = kary_pivots(haystack_cursor_, haystack_last_);

        for (auto&& pivot : pivots) {
          assert(pivot != haystack_last_
            && "Pivot should never be one past the end of the haystack");

          if (std::invoke(compare_, *pivot, *needle_cursor_)) {
            haystack_cursor_ = std::next(pivot);
          } else {
            haystack_last_ = pivot;
            break;
          }
        }

        return init();
      }

      [[nodiscard]] constexpr HaystackI haystack_cursor() const
      {
        return haystack_cursor_;
      }

      [[nodiscard]] constexpr NeedleI needle_cursor() const
      {
        return needle_cursor_;
      }
    };

    struct lower_bound_job_fn {
      template <std::ranges::forward_range Haystack,
        typename Compare,
        std::forward_iterator needle_iterator>
      [[nodiscard]] static constexpr auto operator()(Haystack const& haystack,
        Compare                                                      compare,
        needle_iterator needle_cursor)
      {
        using haystack_iterator =
          std::ranges::iterator_t<std::remove_reference_t<Haystack>>;

        return kary_search_job<haystack_iterator, needle_iterator, Compare>{
          needle_cursor,
          std::ranges::begin(haystack),
          std::ranges::end(haystack),
          compare};
      }
    };

    struct upper_bound_job_fn {
      template <std::ranges::forward_range Haystack,
        typename Compare,
        std::forward_iterator needle_iterator>
      [[nodiscard]] static constexpr auto operator()(Haystack const& haystack,
        Compare                                                      compare,
        needle_iterator needle_cursor)
      {
        auto adapted_compare = [=](auto const& haystrand, auto const& needle) {
          return not std::invoke(compare, needle, haystrand);
        };

        using haystack_iterator =
          std::ranges::iterator_t<std::remove_reference_t<Haystack>>;

        using AdaptedCompare = decltype(std::ref(adapted_compare));

        return kary_search_job<haystack_iterator,
          needle_iterator,
          AdaptedCompare>{needle_cursor,
          std::ranges::begin(haystack),
          std::ranges::end(haystack),
          std::ref(adapted_compare)};
      }
    };

    static constexpr inline lower_bound_job_fn lower_bound_job{};
    static constexpr inline upper_bound_job_fn upper_bound_job{};
  };

} // namespace eytzinger

#endif // SORTED_LAYOUT_POLICY_HPP
