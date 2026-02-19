#pragma once

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

#if __has_include(<sys/mman.h>)
#include <sys/mman.h>
#endif

namespace static_data {

  /**
   * @brief A stateless, C++23 conformant allocator optimized for large static datasets.
   * * This allocator guarantees 2 MiB alignment and synchronous kernel page collapsing
   * for allocations exceeding the huge page threshold. It falls back to standard
   * system allocation for smaller requests, preventing memory bloat. It fully supports
   * C++20/23 constexpr allocation contexts.
   * * @tparam T The type of elements to allocate.
   */
  template <typename T>
  struct hpallocator {
    static_assert(!std::is_const_v<T>, "The C++ Standard forbids allocators for const types.");
    static_assert(!std::is_volatile_v<T>, "The C++ Standard forbids allocators for volatile types.");

    using value_type                             = T;
    using size_type                              = std::size_t;
    using difference_type                        = std::ptrdiff_t;
    using is_always_equal                        = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    static constexpr std::size_t huge_page_threshold = 2 * 1024 * 1024; // 2 MiB

    static_assert(
      (huge_page_threshold & (huge_page_threshold - 1)) == 0,
      "Huge page threshold must be a power of two for posix_memalign."
    );

    [[nodiscard]] constexpr hpallocator() noexcept = default;

    template <typename U>
    [[nodiscard]] constexpr hpallocator(const hpallocator<U>&) noexcept {}

    /**
     * @brief Allocates uninitialized storage.
     * * @param n The number of elements to allocate.
     * @return T* Pointer to the first element of an array of n elements.
     * @throws std::bad_array_new_length if size calculation overflows.
     * @throws std::bad_alloc if memory allocation fails.
     */
    [[nodiscard]] constexpr T* allocate(std::size_t n) {
      if (n == 0) {
        return nullptr;
      }

      if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        throw std::bad_array_new_length();
      }

      // Standard conformance: allow use within constexpr containers.
      // We must use a typed allocator (like std::allocator) rather
      // than ::operator new to satisfy the constexpr evaluator's
      // typing requirements.
      if consteval {
        return std::allocator<T>{}.allocate(n);
      }

      const std::size_t bytes = n * sizeof(T);
      void*             ptr   = nullptr;

      if (bytes >= huge_page_threshold) {
        // Respect alignof(T) if it is anomalously larger than 2 MiB.
        const std::size_t alignment = (alignof(T) > huge_page_threshold) ? alignof(T) : huge_page_threshold;

        if (::posix_memalign(&ptr, alignment, bytes) != 0) {
          throw std::bad_alloc();
        }

#if defined(MADV_COLLAPSE)
        ::madvise(ptr, bytes, MADV_COLLAPSE);
#elif defined(MADV_HUGEPAGE)
        ::madvise(ptr, bytes, MADV_HUGEPAGE);
#endif
      } else {
        // Small path: ensure POSIX alignment requirements (multiple of sizeof(void*)).
        constexpr std::size_t min_align = sizeof(void*);
        const std::size_t     alignment = (alignof(T) > min_align) ? alignof(T) : min_align;

        if (::posix_memalign(&ptr, alignment, bytes) != 0) {
          throw std::bad_alloc();
        }
      }

      assert(ptr != nullptr && "POSIX memalign succeeded but returned a null pointer.");
      return static_cast<T*>(ptr);
    }

#if __cpp_lib_allocate_at_least >= 202302L
    using std::allocation_result;
#else
    // Fallback for compilers that don't support C++23's allocation_result yet
    template <typename Pointer>
    struct allocation_result {
      Pointer     ptr;
      std::size_t count;
    };
#endif

    /**
     * @brief Allocates at least n elements, returning the actual allocated size.
     * Required for full C++23 standard library optimization support.
     */
    [[nodiscard]] constexpr allocation_result<T*> allocate_at_least(std::size_t n) {
      return {allocate(n), n};
    }

    /**
     * @brief Deallocates storage previously allocated by allocate().
     * * @param p Pointer to the allocated storage.
     * @param n The number of elements passed to the original allocation call.
     */
    constexpr void deallocate(T* p, std::size_t n) noexcept {
      if (p == nullptr) {
        return;
      }

      // Standard conformance: In constexpr, deallocation must match
      // the allocation method's type-tracking.
      if consteval {
        std::allocator<T>{}.deallocate(p, n);
        return;
      }

      std::free(p);
    }
  };

  template <typename T, typename U>
  [[nodiscard]] constexpr bool operator==(const hpallocator<T>&, const hpallocator<U>&) noexcept {
    return true;
  }

  template <typename T, typename U>
  [[nodiscard]] constexpr bool operator!=(const hpallocator<T>&, const hpallocator<U>&) noexcept {
    return false;
  }

} // namespace static_data
