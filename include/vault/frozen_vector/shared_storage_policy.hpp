#ifndef FROZEN_SHARED_STORAGE_POLICY_HPP
#define FROZEN_SHARED_STORAGE_POLICY_HPP

#include <algorithm>
#include <cassert>
#include <memory>

namespace frozen {

  template <typename T> struct shared_storage_policy {
    using mutable_handle_type = std::shared_ptr<T[]>;

    template <typename Alloc>
    [[nodiscard]]
    static mutable_handle_type allocate(std::size_t n, const Alloc& a)
    {
      if (n == 0) {
        return mutable_handle_type();
      }
      auto ptr = std::allocate_shared_for_overwrite<T[]>(a, n);
      assert(ptr != nullptr && "Allocation returned null");
      return ptr;
    }

    template <typename Alloc>
    [[nodiscard]]
    static mutable_handle_type
    copy(const mutable_handle_type& src, std::size_t n, const Alloc& a)
    {
      if (!src || n == 0) {
        return mutable_handle_type();
      }
      auto new_data = allocate(n, a);
      assert(new_data && "Allocation failed in copy");
      std::copy(src.get(), src.get() + n, new_data.get());
      return new_data;
    }
  };

} // namespace frozen

#endif // FROZEN_SHARED_STORAGE_POLICY_HPP
