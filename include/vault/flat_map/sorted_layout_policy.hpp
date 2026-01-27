#ifndef SORTED_LAYOUT_POLICY_HPP
#define SORTED_LAYOUT_POLICY_HPP

#include <algorithm>
#include <functional>
#include <iterator>
#include <ranges>
#include <stdexcept>

namespace eytzinger {

  /**
   * @brief Policy class for standard sorted layout (contiguous array).
   * No template parameters required as it mimics standard std::vector behavior.
   */
  struct sorted_layout_policy {
    static constexpr inline const auto UID_V001 = 4185834535822629149uLL;

    // Identity mapping: Physical Index == Sorted Rank
    struct sorted_rank_to_index_fn {
      [[nodiscard]] static constexpr std::size_t
      operator()(std::size_t rank, std::size_t /*n*/) noexcept
      {
        return rank;
      }
    };

    struct index_to_sorted_rank_fn {
      [[nodiscard]] static constexpr std::size_t
      operator()(std::size_t i, std::size_t /*n*/) noexcept
      {
        return i;
      }
    };

    static constexpr inline sorted_rank_to_index_fn sorted_rank_to_index{};
    static constexpr inline index_to_sorted_rank_fn index_to_sorted_rank{};

    // No-op: Data is already sorted
    struct permute_fn {
      template <typename... Args>
      static constexpr void operator()(Args&&...) noexcept
      {}
    };

    // Standard O(1) random access
    struct get_nth_sorted_fn {
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr std::iter_reference_t<I>
      operator()(I first, S last, std::size_t n)
      {
        if (n >= static_cast<std::size_t>(std::distance(first, last))) {
          throw std::out_of_range("sorted_layout_policy index out of range");
        }
        return *(first + n);
      }

      template <std::ranges::random_access_range R>
      [[nodiscard]] static constexpr std::ranges::range_reference_t<R>
      operator()(R&& range, std::size_t n)
      {
        return
        operator()(std::ranges::begin(range), std::ranges::end(range), n);
      }
    };

    // Standard increment: i + 1. Returns -1 if we hit end (n).
    struct next_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t
      operator()(std::ptrdiff_t i, std::size_t n_sz) noexcept
      {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        if (i >= n - 1) {
          return -1;
        }
        return i + 1;
      }
    };

    // Standard decrement: i - 1. If i == -1 (end), returns n - 1.
    struct prev_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t
      operator()(std::ptrdiff_t i, std::size_t n_sz) noexcept
      {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        if (i == -1) {
          return (n == 0) ? -1 : n - 1;
        }
        if (i == 0) {
          return -1; // Going before begin
        }
        return i - 1;
      }
    };

    // Standard Binary Search
    struct lower_bound_fn {
      template <
          std::random_access_iterator I,
          std::sentinel_for<I>        S,
          typename T,
          typename Comp = std::ranges::less,
          typename Proj = std::identity>
      [[nodiscard]] static constexpr I operator()(
          I first, S last, const T& value, Comp comp = {}, Proj proj = {}
      )
      {
        return std::ranges::lower_bound(first, last, value, comp, proj);
      }

      template <
          std::ranges::random_access_range R,
          typename T,
          typename Comp = std::ranges::less,
          typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R>
      operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {})
      {
        return std::ranges::lower_bound(range, value, comp, proj);
      }
    };

    struct upper_bound_fn {
      template <
          std::random_access_iterator I,
          std::sentinel_for<I>        S,
          typename T,
          typename Comp = std::ranges::less,
          typename Proj = std::identity>
      [[nodiscard]] static constexpr I operator()(
          I first, S last, const T& value, Comp comp = {}, Proj proj = {}
      )
      {
        return std::ranges::upper_bound(first, last, value, comp, proj);
      }

      template <
          std::ranges::random_access_range R,
          typename T,
          typename Comp = std::ranges::less,
          typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R>
      operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {})
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
  };

} // namespace eytzinger

#endif // SORTED_LAYOUT_POLICY_HPP
