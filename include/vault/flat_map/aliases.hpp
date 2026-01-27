#ifndef MAPS_HPP
#define MAPS_HPP

#include <algorithm>
#include <bit>
#include <new>
#include <type_traits>

#include "eytzinger_layout_policy.hpp"
#include "implicit_btree_layout_policy.hpp"
#include "layout_map.hpp"
#include "sorted_layout_policy.hpp"

namespace eytzinger::detail {
  [[nodiscard]] consteval std::size_t get_cache_line_size()
  {
#ifdef __cpp_lib_hardware_interference_size
    return std::hardware_destructive_interference_size;
#else
    return 64;
#endif
  }

  template <typename K> [[nodiscard]] consteval int calculate_optimal_prefetch()
  {
    constexpr std::size_t cache_line             = get_cache_line_size();
    constexpr std::size_t target_lookahead_bytes = 4 * cache_line;
    if constexpr (sizeof(K) >= target_lookahead_bytes) {
      return 1;
    } else {
      constexpr std::size_t ratio = target_lookahead_bytes / sizeof(K);
      return std::bit_width(ratio) - 1;
    }
  }

  template <typename K>
  [[nodiscard]] consteval std::size_t calculate_optimal_block_size()
  {
    // 1. SIMD Optimization Check
    // If K is integral and fits within the 64-byte AVX2 width, we force B to
    // match that width to enable the specialized SIMD paths.
    if constexpr (std::is_integral_v<K>) {
      constexpr std::size_t simd_target_bytes = 64;
      if constexpr (sizeof(K) <= simd_target_bytes) {
        return simd_target_bytes / sizeof(K);
      }
    }

    // 2. Default Cache Line Alignment
    constexpr std::size_t cache_line = get_cache_line_size();

    // Ensure stride respects both size and alignment.
    // In standard C++, sizeof(K) is always a multiple of alignof(K), so this
    // max() is theoretically redundant, but it protects against non-standard
    // packed types where sizeof might be smaller than the required alignment
    // stride.
    constexpr std::size_t item_stride = std::max(sizeof(K), alignof(K));

    if constexpr (item_stride >= cache_line) {
      return 1;
    } else {
      return cache_line / item_stride;
    }
  }
} // namespace eytzinger::detail

// Convenience aliases for common map types

namespace eytzinger {

  template <
      typename K,
      typename V,
      typename Compare   = std::less<>,
      typename Allocator = std::allocator<std::pair<const K, V>>>
  using eytzinger_map = layout_map<
      K,
      V,
      Compare,
      eytzinger_layout_policy<detail::calculate_optimal_prefetch<K>()>,
      Allocator>;

  /**
   * @brief A layout_map specialized for the Implicit B-Tree layout.
   * * Configuration:
   * - If K is an integral type, B is automatically configured to 64 bytes
   * to enable AVX2 SIMD optimizations.
   * - Otherwise, B is calculated to fill one CPU cache line, respecting
   * alignment.
   */
  template <
      typename K,
      typename V,
      typename Compare   = std::less<>,
      typename Allocator = std::allocator<std::pair<const K, V>>>
  using btree_map = layout_map<
      K,
      V,
      Compare,
      implicit_btree_layout_policy<detail::calculate_optimal_block_size<K>()>,
      Allocator>;

  template <
      typename K,
      typename V,
      typename Compare   = std::less<>,
      typename Allocator = std::allocator<std::pair<const K, V>>>
  using sorted_map = layout_map<K, V, Compare, sorted_layout_policy, Allocator>;

} // namespace eytzinger

#endif // MAPS_HPP
