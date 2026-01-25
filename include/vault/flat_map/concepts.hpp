#ifndef CONCEPTS_HPP
#define CONCEPTS_HPP

#include <concepts>
#include <iterator>

// clang-format off

namespace eytzinger {

/**
 * @brief Concept for a Layout Policy that supports forward traversal and binary search.
 * * A ForwardLayoutPolicy defines how a sequence of elements is arranged in memory
 * to optimize search operations (e.g., Eytzinger, B-Tree).
 *
 * @tparam P The Policy class.
 * @tparam I The Iterator type of the container (must be Random Access).
 * @tparam C The Comparator type (defaults to std::ranges::less).
 */
template <
    typename P,
    typename I,
    typename C
>
concept ForwardLayoutPolicy = std::random_access_iterator<I> && std::copy_constructible<P> && requires
    (I first, I last, std::size_t n, std::size_t rank, std::ptrdiff_t idx, std::iter_value_t<I> const &key, C comp)
{
    // 1. Index Mapping (Bijective mapping between sorted rank and memory index)
    { P::sorted_rank_to_index(rank, n) } -> std::convertible_to<std::size_t>;
    { P::index_to_sorted_rank(idx, n) }  -> std::convertible_to<std::size_t>;
    
    // 2. Traversal (Forward iteration logic)
    // Must accept a signed index (ptrdiff_t) to handle the "-1" sentinel.
    { P::next_index(idx, n) } -> std::convertible_to<std::ptrdiff_t>;
    
    // 3. Permutation (Reordering data)
    // Must accept a range [first, last)
    P::permute(first, last);
    
    // 4. Access (Retrieving element by sorted rank)
    // Used to implement array-like access in sorted order.
    { P::get_nth_sorted(first, last, n) } -> std::convertible_to<std::iter_reference_t<I>>;
    
    // 5. Search (Binary search variants)
    // Must accept comparator.
    { P::lower_bound(first, last, key, comp) } -> std::same_as<I>;
    { P::upper_bound(first, last, key, comp) } -> std::same_as<I>;
};

/**
 * @brief Concept for a Layout Policy that supports bidirectional traversal.
 * * Refines ForwardLayoutPolicy by requiring a `prev_index` function,
 * enabling reverse iteration over the container.
 */
template <
    typename P,
    typename I,
    typename C
>
concept BidirectionalLayoutPolicy = ForwardLayoutPolicy<P, I, C> && requires
    (std::size_t n, std::ptrdiff_t idx)
{
    // Reverse Traversal
    { P::prev_index(idx, n) } -> std::convertible_to<std::ptrdiff_t>;
};

} // namespace eytzinger

#endif // CONCEPTS_HPP
