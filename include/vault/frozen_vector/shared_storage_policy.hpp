#ifndef FROZEN_SHARED_STORAGE_POLICY_HPP
#define FROZEN_SHARED_STORAGE_POLICY_HPP

#include <memory>

namespace frozen {

template <typename T> struct shared_storage_policy {
  using mutable_handle_type = std::shared_ptr<T[]>;

  template <typename Alloc>
  [[nodiscard]]
  static mutable_handle_type allocate(size_t n, const Alloc &a)
  {
    if (n == 0)
      return nullptr;
    return std::allocate_shared_for_overwrite<T[]>(a, n);
  }
};

} // namespace frozen

#endif // FROZEN_SHARED_STORAGE_POLICY_HPP
