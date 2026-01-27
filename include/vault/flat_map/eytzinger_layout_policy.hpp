#ifndef EYTZINGER_LAYOUT_POLICY_HPP
#define EYTZINGER_LAYOUT_POLICY_HPP

#include <algorithm>
#include <bit>
#include <functional>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define EYTZINGER_PREFETCH(ptr) __builtin_prefetch(ptr, 0, 3)
#elif defined(_MSC_VER)
#include <intrin.h>
#define EYTZINGER_PREFETCH(ptr)                                                \
  _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#else
#define EYTZINGER_PREFETCH(ptr)
#endif

namespace eytzinger {

  template <int L = 6> struct eytzinger_layout_policy {
    static constexpr inline const auto UID_V001 = 16427278603008041617uLL;

  private:
    // Robust calculation of subtree size rooted at i (0-based index)
    [[nodiscard]] static constexpr std::size_t
    count_nodes(std::size_t i, std::size_t n) noexcept
    {
      std::size_t size = 0;
      std::size_t k = i + 1; // Convert to 1-based index for easier level math
      std::size_t p = 1;     // Capacity of current level

      while (k <= n) {
        size += std::min(p, n - k + 1);
        k <<= 1;
        p <<= 1;
      }
      return size;
    }

    [[nodiscard]] static constexpr std::size_t
    restore_lower_bound_index(std::size_t i) noexcept
    {
      std::size_t j = i + 1;
      j >>= (std::countr_one(j) + 1);
      return j == 0 ? static_cast<std::size_t>(-1) : (j - 1);
    }

  public:
    struct sorted_rank_to_index_fn {
      [[nodiscard]] static constexpr std::size_t
      operator()(std::size_t rank, std::size_t n) noexcept
      {
        std::size_t i = 0;
        while (true) {
          std::size_t l_sz = count_nodes((i << 1) + 1, n);
          if (rank == l_sz) {
            return i;
          }
          if (rank < l_sz) {
            i = (i << 1) + 1;
          } else {
            rank -= (l_sz + 1);
            i = (i << 1) + 2;
          }
        }
      }
    };

    struct index_to_sorted_rank_fn {
      [[nodiscard]] static constexpr std::size_t
      operator()(std::size_t i, std::size_t n) noexcept
      {
        std::size_t r = count_nodes((i << 1) + 1, n);
        while (i > 0) {
          if (i % 2 == 0) {
            std::size_t parent = (i - 1) >> 1;
            r += count_nodes((parent << 1) + 1, n) + 1;
          }
          i = (i - 1) >> 1;
        }
        return r;
      }
    };

    static constexpr inline sorted_rank_to_index_fn sorted_rank_to_index{};
    static constexpr inline index_to_sorted_rank_fn index_to_sorted_rank{};

    struct permute_fn {
    private:
      // Recursive In-Order Fill
      // Visits indices k in the order: Left Subtree -> Root -> Right Subtree
      // Since input is sorted, we simply pull the next element from source_iter
      // at each visit.
      template <typename SrcIter, typename TempVec>
      static constexpr void fill_in_order(
          TempVec& temp, SrcIter& source_iter, std::size_t k, std::size_t n
      )
      {
        if (k >= n) {
          return;
        }

        // 1. Recurse Left (2*k + 1)
        fill_in_order(temp, source_iter, 2 * k + 1, n);

        // 2. Visit Root (k)
        temp[k] = std::move(*source_iter);
        ++source_iter;

        // 3. Recurse Right (2*k + 2)
        fill_in_order(temp, source_iter, 2 * k + 2, n);
      }

    public:
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      static constexpr void operator()(I first, S last)
      {
        const auto n = static_cast<std::size_t>(std::distance(first, last));
        if (n <= 1) {
          return;
        }

        // Use iter_value_t to handle proxy iterators correctly (strips
        // references)
        using ValueT = std::iter_value_t<I>;

        std::vector<ValueT> temp;
        temp.resize(n);

        I current_source = first;
        fill_in_order(temp, current_source, 0, n);

        std::ranges::move(temp, first);
      }

      template <std::ranges::random_access_range R>
      static constexpr void operator()(R&& range)
      {
        operator()(std::ranges::begin(range), std::ranges::end(range));
      }
    };

    struct get_nth_sorted_fn {
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr std::iter_reference_t<I>
      operator()(I first, S last, std::size_t n)
      {
        const auto size = static_cast<std::size_t>(std::distance(first, last));
        if (n >= size) {
          throw std::out_of_range("eytzinger index out of range");
        }
        return *(first + sorted_rank_to_index(n, size));
      }

      template <std::ranges::random_access_range R>
      [[nodiscard]] static constexpr std::ranges::range_reference_t<R>
      operator()(R&& range, std::size_t n)
      {
        return
        operator()(std::ranges::begin(range), std::ranges::end(range), n);
      }
    };

    struct next_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t
      operator()(std::ptrdiff_t i, std::size_t n_sz) noexcept
      {
        std::ptrdiff_t n           = static_cast<std::ptrdiff_t>(n_sz);
        auto           right_child = (i << 1) + 2;
        if (right_child < n) {
          i               = right_child;
          auto left_child = (i << 1) + 1;
          while (left_child < n) {
            i          = left_child;
            left_child = (i << 1) + 1;
          }
          return i;
        } else {
          while (i > 0 && !(i & 1)) {
            i = (i - 1) >> 1;
          }
          return (i > 0) ? ((i - 1) >> 1) : -1;
        }
      }
    };

    struct prev_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t
      operator()(std::ptrdiff_t i, std::size_t n_sz) noexcept
      {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        if (i == -1) {
          i = 0;
          while ((i << 1) + 2 < n) {
            i = (i << 1) + 2;
          }
          return i;
        }
        auto left_child = (i << 1) + 1;
        if (left_child < n) {
          i                = left_child;
          auto right_child = (i << 1) + 2;
          while (right_child < n) {
            i           = right_child;
            right_child = (i << 1) + 2;
          }
          return i;
        } else {
          while (i > 0 && (i & 1)) {
            i = (i - 1) >> 1;
          }
          return (i > 0) ? ((i - 1) >> 1) : -1;
        }
      }
    };

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
        if (first == last) {
          return last;
        }
        const auto  n    = static_cast<std::size_t>(std::distance(first, last));
        const auto* base = std::to_address(first);
        std::size_t i    = 0;
        while (i < n) {
          const std::size_t future_i = ((i + 1) << L) - 1;
          EYTZINGER_PREFETCH(&base[future_i]);
          bool go_right = std::invoke(comp, std::invoke(proj, base[i]), value);
          i             = (i << 1) + 1 + static_cast<std::size_t>(go_right);
        }
        std::size_t result_idx = restore_lower_bound_index(i);
        return (result_idx == static_cast<std::size_t>(-1))
                   ? last
                   : (first + result_idx);
      }

      template <
          std::ranges::random_access_range R,
          typename T,
          typename Comp = std::ranges::less,
          typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R>
      operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {})
      {
        return operator()(
            std::ranges::begin(range),
            std::ranges::end(range),
            value,
            std::ref(comp),
            std::ref(proj)
        );
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
        if (first == last) {
          return last;
        }
        const auto  n    = static_cast<std::size_t>(std::distance(first, last));
        const auto* base = std::to_address(first);
        std::size_t i    = 0;
        while (i < n) {
          const std::size_t future_i = ((i + 1) << L) - 1;
          EYTZINGER_PREFETCH(&base[future_i]);
          bool go_right = !std::invoke(comp, value, std::invoke(proj, base[i]));
          i             = (i << 1) + 1 + static_cast<std::size_t>(go_right);
        }
        std::size_t result_idx = restore_lower_bound_index(i);
        return (result_idx == static_cast<std::size_t>(-1))
                   ? last
                   : (first + result_idx);
      }

      template <
          std::ranges::random_access_range R,
          typename T,
          typename Comp = std::ranges::less,
          typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R>
      operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {})
      {
        return operator()(
            std::ranges::begin(range),
            std::ranges::end(range),
            value,
            std::ref(comp),
            std::ref(proj)
        );
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

#endif // EYTZINGER_LAYOUT_POLICY_HPP
