#ifndef SORTED_LAYOUT_POLICY_HPP
#define SORTED_LAYOUT_POLICY_HPP

#include "concepts.hpp"
#include "vault/algorithm/amac.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <ranges>
#include <stdexcept>

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

    // --- AMAC Batch Support ---

    template <typename I, typename T, typename Comp, search_bound Bound>
    struct search_job {
      I              first;
      std::ptrdiff_t count;
      T              key;
      Comp           comp;

      // State
      I              current_mid_it;
      std::ptrdiff_t step_size = 0;

      [[nodiscard]] search_job(I f, std::size_t n, T k, Comp c)
          : first(f)
          , count(static_cast<std::ptrdiff_t>(n))
          , key(std::move(k))
          , comp(c)
          , current_mid_it(f)
      {}

      [[nodiscard]] vault::amac::job_step_result<1> init()
      {
        if (count == 0) {
          return {nullptr};
        }
        step_size      = count / 2;
        current_mid_it = first + step_size;
        return {std::to_address(current_mid_it)};
      }

      [[nodiscard]] vault::amac::job_step_result<1> step()
      {
        // Use IIFE for const-initialization
        bool const go_right = [&] {
          if constexpr (Bound == search_bound::upper) {
            return !std::invoke(comp, key, *current_mid_it);
          } else {
            return std::invoke(comp, *current_mid_it, key);
          }
        }();

        if (go_right) {
          first = ++current_mid_it;
          count -= (step_size + 1);
        } else {
          count = step_size;
        }

        if (count == 0) {
          return {nullptr};
        }

        step_size      = count / 2;
        current_mid_it = first + step_size;
        return {std::to_address(current_mid_it)};
      }

      [[nodiscard]] I result() const { return first; }
    };

    struct batch_lower_bound_fn {
      template <typename I, typename T, typename Comp>
      [[nodiscard]] static auto make_job(
        I first, std::size_t n, T key, Comp comp)
      {
        return search_job<I, T, Comp, search_bound::lower>(
          first, n, std::move(key), comp);
      }
    };

    struct batch_upper_bound_fn {
      template <typename I, typename T, typename Comp>
      [[nodiscard]] static auto make_job(
        I first, std::size_t n, T key, Comp comp)
      {
        return search_job<I, T, Comp, search_bound::upper>(
          first, n, std::move(key), comp);
      }
    };

    static constexpr inline permute_fn           permute{};
    static constexpr inline get_nth_sorted_fn    get_nth_sorted{};
    static constexpr inline next_index_fn        next_index{};
    static constexpr inline prev_index_fn        prev_index{};
    static constexpr inline lower_bound_fn       lower_bound{};
    static constexpr inline upper_bound_fn       upper_bound{};
    static constexpr inline batch_lower_bound_fn batch_lower_bound{};
    static constexpr inline batch_upper_bound_fn batch_upper_bound{};
  };

} // namespace eytzinger

#endif // SORTED_LAYOUT_POLICY_HPP
