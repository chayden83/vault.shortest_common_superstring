#ifndef FROZEN_LOCAL_SHARED_PTR_HPP
#define FROZEN_LOCAL_SHARED_PTR_HPP

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace frozen {

// Forward declaration
template <typename T> class local_shared_ptr;

namespace detail {
struct local_control_block_base {
  long ref_count{1};
  virtual ~local_control_block_base() = default;
};

template <typename T> struct local_control_block : local_control_block_base {};
} // namespace detail

template <typename T, typename Alloc>
local_shared_ptr<T>
allocate_local_shared_for_overwrite(std::size_t n, const Alloc &alloc);

/**
 * A non-atomic, thread-unsafe shared pointer.
 * Optimized for single-threaded immutable containers.
 */
template <typename T> class local_shared_ptr {
public:
  using element_type = std::remove_extent_t<T>;

  // Default Constructor
  constexpr local_shared_ptr() noexcept
      : ptr_(nullptr)
      , cb_(nullptr)
  {}

  // ADDED: Nullptr Constructor (Fixes the compilation error)
  constexpr local_shared_ptr(std::nullptr_t) noexcept
      : ptr_(nullptr)
      , cb_(nullptr)
  {}

  // Destructor
  ~local_shared_ptr()
  {
    release();
  }

  // ADDED: Nullptr Assignment
  local_shared_ptr &operator=(std::nullptr_t) noexcept
  {
    release();
    ptr_ = nullptr;
    cb_ = nullptr;
    return *this;
  }

  // Copy Constructor - Non-Atomic Increment
  local_shared_ptr(const local_shared_ptr &other) noexcept
      : ptr_(other.ptr_)
      , cb_(other.cb_)
  {
    if (cb_) {
      cb_->ref_count++;
    }
  }

  // Copy Assignment
  local_shared_ptr &operator=(const local_shared_ptr &other) noexcept
  {
    if (this != &other) {
      release();
      ptr_ = other.ptr_;
      cb_ = other.cb_;
      if (cb_) {
        cb_->ref_count++;
      }
    }
    return *this;
  }

  // Move Constructor
  local_shared_ptr(local_shared_ptr &&other) noexcept
      : ptr_(other.ptr_)
      , cb_(other.cb_)
  {
    other.ptr_ = nullptr;
    other.cb_ = nullptr;
  }

  // Move Assignment
  local_shared_ptr &operator=(local_shared_ptr &&other) noexcept
  {
    if (this != &other) {
      release();
      ptr_ = other.ptr_;
      cb_ = other.cb_;
      other.ptr_ = nullptr;
      other.cb_ = nullptr;
    }
    return *this;
  }

  element_type &operator[](std::ptrdiff_t i) const noexcept
  {
    return ptr_[i];
  }
  element_type *get() const noexcept
  {
    return ptr_;
  }
  explicit operator bool() const noexcept
  {
    return ptr_ != nullptr;
  }
  long use_count() const noexcept
  {
    return cb_ ? cb_->ref_count : 0;
  }

private:
  element_type *ptr_;
  detail::local_control_block_base *cb_;

  void release()
  {
    if (cb_) {
      if (--cb_->ref_count == 0) {
        if constexpr (!std::is_trivially_destructible_v<element_type>) {
          // Dtor logic would go here in full implementation
        }
        delete[] reinterpret_cast<char *>(cb_);
      }
    }
  }

  template <typename U, typename A>
  friend local_shared_ptr<U>
  allocate_local_shared_for_overwrite(std::size_t, const A &);
};

// ============================================================================
// Factory Implementation
// ============================================================================

template <typename T, typename Alloc>
local_shared_ptr<T>
allocate_local_shared_for_overwrite(std::size_t n, const Alloc &)
{
  using Element = std::remove_extent_t<T>;

  constexpr std::size_t header_size = sizeof(detail::local_control_block_base);
  constexpr std::size_t align = alignof(Element);

  std::size_t padding = 0;
  if (header_size % align != 0) {
    padding = align - (header_size % align);
  }

  std::size_t data_offset = header_size + padding;
  std::size_t total_size = data_offset + (sizeof(Element) * n);

  char *mem = new char[total_size];

  auto *cb = new (mem) detail::local_control_block_base();
  Element *data_ptr = reinterpret_cast<Element *>(mem + data_offset);

  local_shared_ptr<T> ret;
  ret.cb_ = cb;
  ret.ptr_ = data_ptr;
  return ret;
}

} // namespace frozen

#endif // FROZEN_LOCAL_SHARED_PTR_HPP
