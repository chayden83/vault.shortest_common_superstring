#ifndef UTILITIES_HPP
#define UTILITIES_HPP

#include <cstddef>
#include <concepts>

namespace eytzinger {

/**
 * @brief Strong type for Physical/Unordered indices (O(1) access).
 * Used to access elements by their storage index.
 */
template<std::integral I = std::size_t>
struct unordered_index {
    I index_ = {};
    [[nodiscard]] constexpr operator I() const noexcept { return index_; }
};

/**
 * @brief Strong type for Logical/Sorted Rank indices.
 * Used to access the n-th smallest element.
 * Complexity depends on the layout policy (O(log N) for Eytzinger, O(1) for Sorted).
 */
template<std::integral I = std::size_t>
struct ordered_index {
    I index_ = {};
    [[nodiscard]] constexpr operator I() const noexcept { return index_; }
};

} // namespace eytzinger

#endif // UTILITIES_HPP
