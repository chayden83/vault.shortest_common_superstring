#ifndef FROZEN_CONCEPTS_HPP
#define FROZEN_CONCEPTS_HPP

#include <concepts>
#include <cstddef>

namespace frozen {

/**
 * Concept to check if a Policy provides a static 'copy' method
 * compatible with the Handle and Allocator types.
 */
template <typename Policy, typename Handle, typename Alloc>
concept can_copy_handle = requires(Handle h, size_t n, const Alloc &a) {
  { Policy::copy(h, n, a) } -> std::convertible_to<Handle>;
};

} // namespace frozen

#endif // FROZEN_CONCEPTS_HPP
