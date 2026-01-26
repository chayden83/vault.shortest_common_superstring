#ifndef FROZEN_UNIQUE_STORAGE_POLICY_HPP
#define FROZEN_UNIQUE_STORAGE_POLICY_HPP

#include <algorithm>
#include <memory>

namespace frozen {

  template <typename T> struct unique_storage_policy {
    // 1. Defines the Mutable Handle (Unique Ownership)
    using mutable_handle_type = std::unique_ptr<T[]>;

    // 2. Allocation Strategy (No initialization overhead)
    template <typename Alloc>
    [[nodiscard]]
    static mutable_handle_type allocate(size_t n, const Alloc&)
    {
      if (n == 0) {
        return nullptr;
      }
      return std::make_unique_for_overwrite<T[]>(n);
    }

    // 3. Deep Copy Strategy
    // Since unique_ptr is not copyable, providing this method allows the
    // frozen_vector_builder to support copy construction/assignment via deep
    // copy.
    template <typename Alloc>
    [[nodiscard]]
    static mutable_handle_type
    copy(const mutable_handle_type& src, size_t n, const Alloc& a)
    {
      if (!src || n == 0) {
        return nullptr;
      }

      // Allocate new unique storage
      auto new_data = allocate(n, a);

      // Perform element-wise copy
      std::copy(src.get(), src.get() + n, new_data.get());

      return new_data;
    }
  };

} // namespace frozen

#endif // FROZEN_UNIQUE_STORAGE_POLICY_HPP
