#ifndef FROZEN_UNIQUE_STORAGE_POLICY_HPP
#define FROZEN_UNIQUE_STORAGE_POLICY_HPP

#include <algorithm>
#include <cassert>
#include <memory>

namespace frozen {

  template <typename T> struct unique_storage_policy {
    using mutable_handle_type = std::unique_ptr<T[]>;

    template <typename Alloc>
    [[nodiscard]]
    static mutable_handle_type allocate(std::size_t n, const Alloc& a)
    {
      if (n == 0) {
        return nullptr;
      }
      // unique_ptr doesn't have an allocate_shared equivalent for custom
      // allocators easily, using standard make_unique_for_overwrite for
      // simplicity
      auto ptr = std::make_unique_for_overwrite<T[]>(n);
      assert(ptr != nullptr);
      return ptr;
    }

    template <typename Alloc>
    [[nodiscard]]
    static mutable_handle_type
    copy(const mutable_handle_type& src, std::size_t n, const Alloc& a)
    {
      if (!src || n == 0) {
        return nullptr;
      }
      auto new_data = allocate(n, a);
      assert(new_data != nullptr);
      std::copy(src.get(), src.get() + n, new_data.get());
      return new_data;
    }
  };

} // namespace frozen

#endif // FROZEN_UNIQUE_STORAGE_POLICY_HPP
