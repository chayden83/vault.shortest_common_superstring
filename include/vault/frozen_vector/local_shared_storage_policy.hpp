#ifndef FROZEN_LOCAL_SHARED_STORAGE_POLICY_HPP
#define FROZEN_LOCAL_SHARED_STORAGE_POLICY_HPP

#include "local_shared_ptr.hpp"

namespace frozen {

template <typename T> struct local_shared_storage_policy {
  // 1. Mutable Handle uses our custom ptr
  using mutable_handle_type = local_shared_ptr<T[]>;

  // 2. Allocation Strategy
  template <typename Alloc>
  [[nodiscard]]
  static mutable_handle_type allocate(std::size_t n, const Alloc &a)
  {
    if (n == 0)
      return mutable_handle_type();
    return allocate_local_shared_for_overwrite<T[]>(n, a);
  }

  // 3. Copy Strategy (Enable deep copying of builder)
  template <typename Alloc>
  [[nodiscard]]
  static mutable_handle_type
  copy(const mutable_handle_type &src, std::size_t n, const Alloc &a)
  {
    if (!src || n == 0)
      return mutable_handle_type();
    auto new_data = allocate(n, a);
    std::copy(src.get(), src.get() + n, new_data.get());
    return new_data;
  }
};

} // namespace frozen

#endif // FROZEN_LOCAL_SHARED_STORAGE_POLICY_HPP
