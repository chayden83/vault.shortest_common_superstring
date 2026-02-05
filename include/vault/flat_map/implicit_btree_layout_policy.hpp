#ifndef IMPLICIT_BTREE_LAYOUT_POLICY_HPP
#define IMPLICIT_BTREE_LAYOUT_POLICY_HPP

#include "concepts.hpp"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <vault/algorithm/amac.hpp>

// Note: Explicit prefetch macros removed; prefetching is handled by the AMAC
// executor.

#if defined(__AVX2__)
#define LAYOUT_USE_AVX2 1
#endif

namespace eytzinger {

  // --- Concepts ---

  template <typename Comp, typename T>
  concept IsStandardLess = std::same_as<Comp, std::less<T>>
    || std::same_as<Comp, std::less<void>> || std::same_as<Comp, std::less<>>
    || std::same_as<Comp, std::ranges::less>;

  template <typename Comp, typename T>
  concept IsStandardGreater = std::same_as<Comp, std::greater<T>>
    || std::same_as<Comp, std::greater<void>>
    || std::same_as<Comp, std::greater<>>
    || std::same_as<Comp, std::ranges::greater>;

  // --- Internal Declarations ---
  // These are implemented in implicit_btree_layout_policy.cpp
  namespace detail {
    [[nodiscard]] std::size_t btree_child_block_index(
      std::size_t block_idx, std::size_t child_slot, std::size_t B);
    [[nodiscard]] std::size_t btree_sorted_rank_to_index(
      std::size_t rank, std::size_t n, std::size_t B);
    [[nodiscard]] std::size_t btree_index_to_sorted_rank(
      std::size_t index, std::size_t n, std::size_t B);
    [[nodiscard]] std::ptrdiff_t btree_next_index(
      std::ptrdiff_t i, std::size_t n, std::size_t B);
    [[nodiscard]] std::ptrdiff_t btree_prev_index(
      std::ptrdiff_t i, std::size_t n, std::size_t B);

#ifdef LAYOUT_USE_AVX2
    [[nodiscard]] std::size_t search_loop_lb_uint64_less(
      const uint64_t* base, std::size_t n, uint64_t key, std::size_t B);
    [[nodiscard]] std::size_t search_loop_ub_uint64_less(
      const uint64_t* base, std::size_t n, uint64_t key, std::size_t B);
    [[nodiscard]] std::size_t search_loop_lb_int64_less(
      const int64_t* base, std::size_t n, int64_t key, std::size_t B);
    [[nodiscard]] std::size_t search_loop_ub_int64_less(
      const int64_t* base, std::size_t n, int64_t key, std::size_t B);

    [[nodiscard]] std::size_t simd_lb_int64_less(const int64_t* b, int64_t k);
    std::size_t               simd_ub_int64_less(const int64_t* b, int64_t k);
    [[nodiscard]] std::size_t simd_lb_uint64_less(
      const uint64_t* b, uint64_t k);
    std::size_t simd_ub_uint64_less(const uint64_t* b, uint64_t k);
    [[nodiscard]] std::size_t simd_lb_int64_greater(
      const int64_t* b, int64_t k);
    std::size_t simd_ub_int64_greater(const int64_t* b, int64_t k);
    [[nodiscard]] std::size_t simd_lb_uint64_greater(
      const uint64_t* b, uint64_t k);
    std::size_t simd_ub_uint64_greater(const uint64_t* b, uint64_t k);

    [[nodiscard]] std::size_t simd_lb_int32_less(const int32_t* b, int32_t k);
    std::size_t               simd_ub_int32_less(const int32_t* b, int32_t k);
    [[nodiscard]] std::size_t simd_lb_uint32_less(
      const uint32_t* b, uint32_t k);
    std::size_t simd_ub_uint32_less(const uint32_t* b, uint32_t k);
    [[nodiscard]] std::size_t simd_lb_int32_greater(
      const int32_t* b, int32_t k);
    std::size_t simd_ub_int32_greater(const int32_t* b, int32_t k);
    [[nodiscard]] std::size_t simd_lb_uint32_greater(
      const uint32_t* b, uint32_t k);
    std::size_t simd_ub_uint32_greater(const uint32_t* b, uint32_t k);

    [[nodiscard]] std::size_t simd_lb_int16_less(const int16_t* b, int16_t k);
    std::size_t               simd_ub_int16_less(const int16_t* b, int16_t k);
    [[nodiscard]] std::size_t simd_lb_uint16_less(
      const uint16_t* b, uint16_t k);
    std::size_t simd_ub_uint16_less(const uint16_t* b, uint16_t k);
    [[nodiscard]] std::size_t simd_lb_int16_greater(
      const int16_t* b, int16_t k);
    std::size_t simd_ub_int16_greater(const int16_t* b, int16_t k);
    [[nodiscard]] std::size_t simd_lb_uint16_greater(
      const uint16_t* b, uint16_t k);
    std::size_t simd_ub_uint16_greater(const uint16_t* b, uint16_t k);

    [[nodiscard]] std::size_t simd_lb_int8_less(const int8_t* b, int8_t k);
    std::size_t               simd_ub_int8_less(const int8_t* b, int8_t k);
    [[nodiscard]] std::size_t simd_lb_uint8_less(const uint8_t* b, uint8_t k);
    std::size_t               simd_ub_uint8_less(const uint8_t* b, uint8_t k);
    [[nodiscard]] std::size_t simd_lb_int8_greater(const int8_t* b, int8_t k);
    std::size_t               simd_ub_int8_greater(const int8_t* b, int8_t k);
    [[nodiscard]] std::size_t simd_lb_uint8_greater(
      const uint8_t* b, uint8_t k);
    std::size_t simd_ub_uint8_greater(const uint8_t* b, uint8_t k);
#endif
  } // namespace detail

  // --- Scalar Fallback Helper ---

  struct scalar_block_searcher {
    template <typename T, std::size_t B, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t lower_bound(
      const T* block, const T& key, Comp comp, Proj proj)
    {
      assert(block != nullptr && "Block pointer must not be null");
      std::size_t i = 0;
      for (; i < B; ++i) {
        if (!std::invoke(comp, std::invoke(proj, block[i]), key)) {
          return i;
        }
      }
      return B;
    }

    template <typename T, std::size_t B, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t upper_bound(
      const T* block, const T& key, Comp comp, Proj proj)
    {
      assert(block != nullptr && "Block pointer must not be null");
      std::size_t i = 0;
      for (; i < B; ++i) {
        if (std::invoke(comp, key, std::invoke(proj, block[i]))) {
          return i;
        }
      }
      return B;
    }

    template <typename T, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t lower_bound_n(
      const T* block, std::size_t n, const T& key, Comp comp, Proj proj)
    {
      assert(block != nullptr || n == 0);
      std::size_t i = 0;
      for (; i < n; ++i) {
        if (!std::invoke(comp, std::invoke(proj, block[i]), key)) {
          return i;
        }
      }
      return n;
    }

    template <typename T, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t upper_bound_n(
      const T* block, std::size_t n, const T& key, Comp comp, Proj proj)
    {
      assert(block != nullptr || n == 0);
      std::size_t i = 0;
      for (; i < n; ++i) {
        if (std::invoke(comp, key, std::invoke(proj, block[i]))) {
          return i;
        }
      }
      return n;
    }
  };

  // --- Block Searcher Primary Template ---

  template <typename T, typename Comp, std::size_t B> struct block_searcher {
    template <typename Proj>
    [[nodiscard]] static constexpr std::size_t lower_bound(
      const T* block, const T& key, Comp comp, Proj proj)
    {
      return scalar_block_searcher::lower_bound<T, B>(block, key, comp, proj);
    }

    template <typename Proj>
    [[nodiscard]] static constexpr std::size_t upper_bound(
      const T* block, const T& key, Comp comp, Proj proj)
    {
      return scalar_block_searcher::upper_bound<T, B>(block, key, comp, proj);
    }
  };

#ifdef LAYOUT_USE_AVX2
#define DEFINE_SIMD_SPECIALIZATION(TYPE, BLOCK_SIZE, CONCEPT, SUFFIX)          \
  template <typename Comp>                                                     \
    requires CONCEPT<Comp, TYPE##_t>                                           \
  struct block_searcher<TYPE##_t, Comp, BLOCK_SIZE> {                          \
    template <typename Proj>                                                   \
    [[nodiscard]] static std::size_t lower_bound(                              \
      const TYPE##_t* block, const TYPE##_t& key, Comp comp, Proj proj)        \
    {                                                                          \
      assert((reinterpret_cast<uintptr_t>(block) % alignof(TYPE##_t) == 0)     \
        && "Block pointer should be aligned for SIMD");                        \
      if constexpr (std::is_same_v<Proj, std::identity>) {                     \
        return detail::simd_lb_##TYPE##_##SUFFIX(block, key);                  \
      } else {                                                                 \
        return scalar_block_searcher::lower_bound<TYPE##_t, BLOCK_SIZE>(       \
          block, key, comp, proj);                                             \
      }                                                                        \
    }                                                                          \
    template <typename Proj>                                                   \
    [[nodiscard]] static std::size_t upper_bound(                              \
      const TYPE##_t* block, const TYPE##_t& key, Comp comp, Proj proj)        \
    {                                                                          \
      assert((reinterpret_cast<uintptr_t>(block) % alignof(TYPE##_t) == 0)     \
        && "Block pointer should be aligned for SIMD");                        \
      if constexpr (std::is_same_v<Proj, std::identity>) {                     \
        return detail::simd_ub_##TYPE##_##SUFFIX(block, key);                  \
      } else {                                                                 \
        return scalar_block_searcher::upper_bound<TYPE##_t, BLOCK_SIZE>(       \
          block, key, comp, proj);                                             \
      }                                                                        \
    }                                                                          \
  };

  DEFINE_SIMD_SPECIALIZATION(int64, 8, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(uint64, 8, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(int64, 8, IsStandardGreater, greater)
  DEFINE_SIMD_SPECIALIZATION(uint64, 8, IsStandardGreater, greater)

  DEFINE_SIMD_SPECIALIZATION(int32, 16, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(uint32, 16, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(int32, 16, IsStandardGreater, greater)
  DEFINE_SIMD_SPECIALIZATION(uint32, 16, IsStandardGreater, greater)

  DEFINE_SIMD_SPECIALIZATION(int16, 32, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(uint16, 32, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(int16, 32, IsStandardGreater, greater)
  DEFINE_SIMD_SPECIALIZATION(uint16, 32, IsStandardGreater, greater)

  DEFINE_SIMD_SPECIALIZATION(int8, 64, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(uint8, 64, IsStandardLess, less)
  DEFINE_SIMD_SPECIALIZATION(int8, 64, IsStandardGreater, greater)
  DEFINE_SIMD_SPECIALIZATION(uint8, 64, IsStandardGreater, greater)

#undef DEFINE_SIMD_SPECIALIZATION
#endif

  // --- Main Layout Policy ---

  template <std::size_t B = 16> struct implicit_btree_layout_policy {
    static_assert(B >= 2, "Block size B must be at least 2");
    static_assert((B % 2 == 0),
      "Block size should be even for proper alignment/node sizing");

    static constexpr inline auto const ARITY  = B + 1;
    static constexpr inline auto const FANOUT = 1;

    static constexpr inline auto const UID_V001 = 15922480214965706541uLL;

    template <typename I> struct is_compatible_key_iterator {
      static constexpr bool value = std::contiguous_iterator<I>;
    };

    // --- Public Logic (Iterators/Index Math) ---

    struct sorted_rank_to_index_fn {
      [[nodiscard]] constexpr std::size_t operator()(
        std::size_t rank, std::size_t n) const
      {
        assert(rank < n && "Rank out of bounds");
        return detail::btree_sorted_rank_to_index(rank, n, B);
      }
    };

    struct index_to_sorted_rank_fn {
      [[nodiscard]] constexpr std::size_t operator()(
        std::size_t index, std::size_t n) const
      {
        assert(index < n && "Index out of bounds");
        return detail::btree_index_to_sorted_rank(index, n, B);
      }
    };

    struct next_index_fn {
      [[nodiscard]] constexpr std::ptrdiff_t operator()(
        std::ptrdiff_t i, std::size_t n_sz) const
      {
        assert(i >= 0 && i < static_cast<std::ptrdiff_t>(n_sz));
        std::ptrdiff_t res = detail::btree_next_index(i, n_sz, B);
        return (res == -1) ? static_cast<std::ptrdiff_t>(n_sz) : res;
      }
    };

    struct prev_index_fn {
      [[nodiscard]] constexpr std::ptrdiff_t operator()(
        std::ptrdiff_t i, std::size_t n_sz) const
      {
        // Handle n_sz (end) as -1 for detail impl
        std::ptrdiff_t input_i = (i == static_cast<std::ptrdiff_t>(n_sz)) ? -1
                                                                          : i;
        std::ptrdiff_t res     = detail::btree_prev_index(input_i, n_sz, B);
        // Map output -1 (underflow) to n_sz
        return (res == -1) ? static_cast<std::ptrdiff_t>(n_sz) : res;
      }
    };

    static constexpr inline sorted_rank_to_index_fn sorted_rank_to_index{};
    static constexpr inline index_to_sorted_rank_fn index_to_sorted_rank{};

    static constexpr inline next_index_fn next_index{};
    static constexpr inline prev_index_fn prev_index{};

    struct permute_fn {
    private:
      template <typename SrcIter, typename TempVec>
      static constexpr void fill_in_order(TempVec& temp,
        SrcIter&                                   source_iter,
        std::size_t                                block_idx,
        std::size_t                                n)
      {
        std::size_t block_start = block_idx * B;
        if (block_start >= n) {
          return;
        }
        for (std::size_t i = 0; i <= B; ++i) {
          std::size_t child = detail::btree_child_block_index(block_idx, i, B);
          if (child * B < n) {
            fill_in_order(temp, source_iter, child, n);
          }
          if (i < B) {
            std::size_t key_idx = block_start + i;
            if (key_idx < n) {
              assert(key_idx < temp.size()
                && "Permutation target index out of bounds");
              temp[key_idx] = *source_iter;
              ++source_iter;
            }
          }
        }
      }

    public:
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      static constexpr void operator()(I first, S last)
      {
        const auto n = static_cast<std::size_t>(std::distance(first, last));
        if (n <= 1) {
          return;
        }
        using ValueT = std::iter_value_t<I>;
        std::vector<ValueT> temp;
        temp.resize(n);
        I current_source = first;
        fill_in_order(temp, current_source, 0, n);

        assert(
          std::distance(first, current_source) == static_cast<std::ptrdiff_t>(n)
          && "Not all elements consumed");
        std::ranges::move(temp, first);
      }

      template <std::ranges::random_access_range R>
      static constexpr void operator()(R&& range)
      {
        operator()(std::ranges::begin(range), std::ranges::end(range));
      }
    };

    static constexpr inline permute_fn permute{};

    struct get_nth_sorted_fn {
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] constexpr std::iter_reference_t<I> operator()(
        I first, S last, std::size_t n) const
      {
        const auto size = static_cast<std::size_t>(std::distance(first, last));
        if (n >= size) {
          throw std::out_of_range("eytzinger index out of range");
        }
        assert(n < size);
        return *(first + sorted_rank_to_index(n, size));
      }

      template <std::ranges::random_access_range R>
      [[nodiscard]] constexpr std::ranges::range_reference_t<R> operator()(
        R&& range, std::size_t n) const
      {
        return (*this)(std::ranges::begin(range), std::ranges::end(range), n);
      }
    };

    static constexpr inline get_nth_sorted_fn get_nth_sorted{};

    // --- Wrappers for Synchronous Use ---

    struct lower_bound_fn {
      template <std::random_access_iterator I,
        std::sentinel_for<I>                S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr I operator()(
        I first, S last, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        if (first == last) {
          return last;
        }
        const auto  n    = static_cast<std::size_t>(std::distance(first, last));
        const auto* base = std::to_address(first);
        assert(base != nullptr && "Search base pointer is null");

#ifdef LAYOUT_USE_AVX2
        if constexpr (B == 8 && std::is_same_v<T, uint64_t>
          && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
          std::size_t idx =
            detail::search_loop_lb_uint64_less(base, n, value, B);
          return (idx == n) ? last : (first + idx);
        } else if constexpr (B == 8 && std::is_same_v<T, int64_t>
          && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
          std::size_t idx =
            detail::search_loop_lb_int64_less(base, n, value, B);
          return (idx == n) ? last : (first + idx);
        } else
#endif
        {
          // Generic fallback logic
          std::size_t k          = 0;
          std::size_t result_idx = n;
          while (true) {
            std::size_t block_start = k * B;
            if (block_start >= n) {
              break;
            }

            if (block_start + B <= n) {
              std::size_t idx = block_searcher<T, Comp, B>::lower_bound(
                base + block_start, value, comp, proj);
              if (idx < B) {
                result_idx = block_start + idx;
              }
              k = detail::btree_child_block_index(k, idx, B);
            } else {
              std::size_t count = n - block_start;
              std::size_t idx   = scalar_block_searcher::lower_bound_n(
                base + block_start, count, value, comp, proj);
              if (idx < count) {
                result_idx = block_start + idx;
                k          = detail::btree_child_block_index(k, idx, B);
              } else {
                k = detail::btree_child_block_index(k, count, B);
              }
            }
          }
          return (result_idx == n) ? last : (first + result_idx);
        }
      }

      template <std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr std::ranges::iterator_t<R> operator()(
        R&& range, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        return operator()(std::ranges::begin(range),
          std::ranges::end(range),
          value,
          std::ref(comp),
          std::ref(proj));
      }
    };

    struct upper_bound_fn {
      template <std::random_access_iterator I,
        std::sentinel_for<I>                S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr I operator()(
        I first, S last, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        if (first == last) {
          return last;
        }
        const auto  n    = static_cast<std::size_t>(std::distance(first, last));
        const auto* base = std::to_address(first);
        assert(base != nullptr && "Search base pointer is null");

#ifdef LAYOUT_USE_AVX2
        if constexpr (B == 8 && std::is_same_v<T, uint64_t>
          && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
          std::size_t idx =
            detail::search_loop_ub_uint64_less(base, n, value, B);
          return (idx == n) ? last : (first + idx);
        } else if constexpr (B == 8 && std::is_same_v<T, int64_t>
          && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
          std::size_t idx =
            detail::search_loop_ub_int64_less(base, n, value, B);
          return (idx == n) ? last : (first + idx);
        } else
#endif
        {
          // Generic fallback logic
          std::size_t k          = 0;
          std::size_t result_idx = n;
          while (true) {
            std::size_t block_start = k * B;
            if (block_start >= n) {
              break;
            }

            if (block_start + B <= n) {
              std::size_t idx = block_searcher<T, Comp, B>::upper_bound(
                base + block_start, value, comp, proj);
              if (idx < B) {
                result_idx = block_start + idx;
              }
              k = detail::btree_child_block_index(k, idx, B);
            } else {
              std::size_t count = n - block_start;
              std::size_t idx   = scalar_block_searcher::upper_bound_n(
                base + block_start, count, value, comp, proj);
              if (idx < count) {
                result_idx = block_start + idx;
                k          = detail::btree_child_block_index(k, idx, B);
              } else {
                k = detail::btree_child_block_index(k, count, B);
              }
            }
          }
          return (result_idx == n) ? last : (first + result_idx);
        }
      }

      template <std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr std::ranges::iterator_t<R> operator()(
        R&& range, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        return operator()(std::ranges::begin(range),
          std::ranges::end(range),
          value,
          std::ref(comp),
          std::ref(proj));
      }
    };

    static constexpr inline lower_bound_fn lower_bound{};
    static constexpr inline upper_bound_fn upper_bound{};

    // --- AMAC Implementation ---

    template <typename HaystackIter, typename NeedleIter> struct search_state {
      HaystackIter begin_it;
      std::size_t  n;
      NeedleIter   needle_iter;
      std::size_t  k = 0;
      std::size_t  result_idx;
    };

    template <typename Compare> struct search_context {
      [[no_unique_address]] Compare compare_;

      static constexpr uint64_t fanout() { return FANOUT; }

      // Accessor for the reporter
      template <typename State>
      [[nodiscard]] auto get_result(State const& s) const
      {
        return (s.result_idx == s.n) ? (s.begin_it + s.n)
                                     : (s.begin_it + s.result_idx);
      }

      template <typename State, typename Emit>
      [[nodiscard]] constexpr vault::amac::step_result<FANOUT> init(
        State& s, Emit&&) const
      {
        if (s.n == 0) {
          return {nullptr};
        }
        return {std::to_address(s.begin_it)};
      }

      template <typename State, typename Emit>
      [[nodiscard]] constexpr vault::amac::step_result<FANOUT> step(
        State& s, Emit&& emit) const
      {
        using ValT              = std::iter_value_t<decltype(s.begin_it)>;
        std::size_t block_start = s.k * B;

        if (block_start >= s.n) {
          return {nullptr};
        }

        if (block_start + B <= s.n) {
          const auto* block_ptr = std::to_address(s.begin_it + block_start);
          std::size_t idx       = block_searcher<ValT, Compare, B>::lower_bound(
            block_ptr, *s.needle_iter, compare_, std::identity{});

          if (idx < B) {
            s.result_idx = block_start + idx;
          }
          s.k = detail::btree_child_block_index(s.k, idx, B);
        } else {
          std::size_t count     = s.n - block_start;
          const auto* block_ptr = std::to_address(s.begin_it + block_start);
          std::size_t idx       = scalar_block_searcher::lower_bound_n(
            block_ptr, count, *s.needle_iter, compare_, std::identity{});

          if (idx < count) {
            s.result_idx = block_start + idx;
            s.k          = detail::btree_child_block_index(s.k, idx, B);
          } else {
            s.k = detail::btree_child_block_index(s.k, count, B);
          }
        }

        if (s.k * B >= s.n) {
          return {nullptr};
        }
        return {std::to_address(s.begin_it + (s.k * B))};
      }
    };

    struct search_state_fn {
      template <std::ranges::random_access_range Haystack, typename NeedleIter>
      [[nodiscard]] constexpr auto operator()(
        Haystack const& haystack, NeedleIter needle) const
      {
        using HaystackI =
          std::ranges::iterator_t<std::remove_reference_t<Haystack const>>;
        return search_state<HaystackI, NeedleIter>{
          std::ranges::begin(haystack),
          std::ranges::size(haystack),
          needle,
          0,                          // k
          std::ranges::size(haystack) // result_idx = n
        };
      }
    };

    struct lower_bound_context_fn {
      template <typename Compare = std::ranges::less>
      [[nodiscard]] constexpr auto operator()(Compare compare = {}) const
      {
        return search_context<Compare>{compare};
      }
    };

    struct upper_bound_context_fn {
      template <typename Compare = std::ranges::less>
      [[nodiscard]] constexpr auto operator()(Compare compare = {}) const
      {
        // Fix: Invert condition for Upper Bound.
        // LB predicate: elem < val (false -> elem >= val)
        // UB predicate: elem <= val (false -> elem > val)
        // elem <= val  <==>  !(val < elem)  <==> !comp(val, elem)
        auto adapted = [=](auto const& node, auto const& needle) {
          return !std::invoke(compare, needle, node);
        };
        return search_context<decltype(adapted)>{adapted};
      }
    };

    static constexpr inline search_state_fn        make_state{};
    static constexpr inline lower_bound_context_fn lower_bound_context{};
    static constexpr inline upper_bound_context_fn upper_bound_context{};
  };

} // namespace eytzinger

#endif // IMPLICIT_BTREE_LAYOUT_POLICY_HPP
