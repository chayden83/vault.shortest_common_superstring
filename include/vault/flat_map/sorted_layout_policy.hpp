#ifndef SORTED_LAYOUT_POLICY_HPP
#define SORTED_LAYOUT_POLICY_HPP

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <type_traits>

#include <vault/algorithm/amac.hpp>

namespace eytzinger {

  struct sorted_layout_policy {
    static constexpr inline const auto UID_V001 = 4185834535822629149uLL;

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
        assert(i >= -1 && i < n && "Index out of bounds");
        if (i >= n - 1) {
          return -1;
        }
        return i + 1;
      }
    };

    struct prev_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t operator()(
        std::ptrdiff_t i, std::size_t n_sz) noexcept
      {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        assert(i >= -1 && i < n && "Index out of bounds");
        if (i == -1) {
          return (n == 0) ? -1 : n - 1;
        }
        if (i == 0) {
          return -1;
        }
        return i - 1;
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
    struct batch_job {
      NeedleI needle_cursor_;

      HaystackI haystack_cursor_;
      HaystackI haystack_last_;

      [[no_unique_address]] Compare compare_;

      [[nodiscard]] constexpr vault::amac::job_step_result<1> init()
      {
        if (haystack_cursor_ == haystack_last_) {
          return {nullptr};
        } else {
          return {
            std::addressof(*std::midpoint(haystack_cursor_, haystack_last_))};
        }
      }

      [[nodiscard]] constexpr vault::amac::job_step_result<1> step()
      {
        auto middle = std::midpoint(haystack_cursor_, haystack_last_);

        if (std::invoke(compare_, *middle, *needle_cursor_)) {
          haystack_cursor_ = std::next(middle);
        } else {
          haystack_last_ = middle;
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

        return batch_job<haystack_iterator, needle_iterator, Compare>{
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

        return batch_job<haystack_iterator, needle_iterator, Compare>{
          needle_cursor,
          std::ranges::begin(haystack),
          std::ranges::end(haystack),
          adapted_compare};
      }
    };

    static constexpr inline lower_bound_job_fn lower_bound_job{};
    static constexpr inline upper_bound_job_fn upper_bound_job{};
  };

} // namespace eytzinger

#endif // SORTED_LAYOUT_POLICY_HPP
