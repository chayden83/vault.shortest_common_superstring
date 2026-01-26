#ifndef FROZEN_FREEZE_TRAITS_HPP
#define FROZEN_FREEZE_TRAITS_HPP

#include <memory>
#include <utility>

#include "local_shared_ptr.hpp"

namespace frozen {

// Primary template
template <typename MutableHandle, typename ConstHandle> struct freeze_traits;

// Specialization 1: std::shared_ptr<T[]> -> std::shared_ptr<const T[]>
template <typename T>
struct freeze_traits<std::shared_ptr<T[]>, std::shared_ptr<const T[]>> {
  [[nodiscard]]
  static std::shared_ptr<const T[]> freeze(std::shared_ptr<T[]> &&h) noexcept
  {
    return std::move(h);
  }
};

// Specialization 2: std::unique_ptr<T[]> -> std::shared_ptr<const T[]>
template <typename T>
struct freeze_traits<std::unique_ptr<T[]>, std::shared_ptr<const T[]>> {
  [[nodiscard]]
  static std::shared_ptr<const T[]> freeze(std::unique_ptr<T[]> &&h)
  {
    return std::shared_ptr<const T[]>(std::move(h));
  }
};

// Specialization 3: local_shared_ptr<T[]> -> local_shared_ptr<const T[]>
// (Requires adding const T[] support to local_shared_ptr,
// OR we can just cast the pointer type if the class supports it.
// For simplicity here, we assume user freezes to the SAME handle type with
// const T)

template <typename T>
struct freeze_traits<local_shared_ptr<T[]>, local_shared_ptr<const T[]>> {
  [[nodiscard]]
  static local_shared_ptr<const T[]> freeze(local_shared_ptr<T[]> &&h) noexcept
  {
    // Since our simple implementation doesn't have sophisticated converting
    // constructors, we will implement a reinterpret_cast equivalent via move.
    // In a full implementation, local_shared_ptr should have a converting ctor.

    // Hack for this implementation: Rely on the binary layout being identical.
    return reinterpret_cast<local_shared_ptr<const T[]> &&>(h);
  }
};

} // namespace frozen

#endif // FROZEN_FREEZE_TRAITS_HPP
