#ifndef CONCEPTS_HPP
#define CONCEPTS_HPP

#include <concepts>
#include <iterator>

namespace eytzinger {

template <typename P, typename I, typename C>
concept OrderedLayoutPolicy =
    std::random_access_iterator<I> && std::copy_constructible<P> &&
    requires(
        I first,
        I last,
        std::size_t n,
        std::size_t rank,
        std::ptrdiff_t idx,
        std::iter_value_t<I> const &key,
        C comp
    ) {
      // 1. Index Mapping (Bijective mapping between sorted rank and memory
      // index)
      { P::sorted_rank_to_index(rank, n) } -> std::convertible_to<std::size_t>;
      { P::index_to_sorted_rank(idx, n) } -> std::convertible_to<std::size_t>;

      // 2. Permutation (Reordering data)
      // Must accept a range [first, last)
      P::permute(first, last);

      // 3. Access (Retrieving element by sorted rank)
      // Used to implement array-like access in sorted order.
      {
        P::get_nth_sorted(first, last, n)
      } -> std::convertible_to<std::iter_reference_t<I>>;

      // 4. Search (Binary search variants)
      // Must accept comparator.
      { P::lower_bound(first, last, key, comp) } -> std::same_as<I>;
      { P::upper_bound(first, last, key, comp) } -> std::same_as<I>;
    };

template <typename P>
concept ForwardLayoutPolicy =
    std::copy_constructible<P> && requires(std::size_t n, std::ptrdiff_t idx) {
      { P::next_index(idx, n) } -> std::convertible_to<std::ptrdiff_t>;
    };

template <typename P>
concept BidirectionalLayoutPolicy =
    ForwardLayoutPolicy<P> && requires(std::size_t n, std::ptrdiff_t idx) {
      { P::prev_index(idx, n) } -> std::convertible_to<std::ptrdiff_t>;
    };

template <typename P, typename I, typename C>
concept OrderedForwardLayoutPolicy =
    OrderedLayoutPolicy<P, I, C> && ForwardLayoutPolicy<P>;

template <typename P, typename I, typename C>
concept OrderedBidirectionalLayoutPolicy =
    OrderedLayoutPolicy<P, I, C> && BidirectionalLayoutPolicy<P>;

} // namespace eytzinger

#endif // CONCEPTS_HPP
