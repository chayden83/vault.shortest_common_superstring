#ifndef FROZEN_SHARED_STORAGE_POLICY_HPP
#define FROZEN_SHARED_STORAGE_POLICY_HPP

#include <algorithm>
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
      return std::allocate_shared_for_overwrite<T[]>(a, n);
    }

    // ADDED: Copy method for builder deep-copy support
    template <typename Alloc>
    [[nodiscard]]
    static mutable_handle_type
    copy(const mutable_handle_type& src, std::size_t n, const Alloc& a)
    {
      if (!src || n == 0) {
        return mutable_handle_type();
      }
      auto new_data = allocate(n, a);
      std::copy(src.get(), src.get() + n, new_data.get());
      return new_data;
    }
  };

} // namespace frozen

#endif // FROZEN_SHARED_STORAGE_POLICY_HPP
