#ifndef FROZEN_LOCAL_SHARED_PTR_HPP
#define FROZEN_LOCAL_SHARED_PTR_HPP

#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "traits.hpp"

namespace frozen {

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
 */
template <typename T> class local_shared_ptr {
public:
  using element_type = std::remove_extent_t<T>;

  constexpr local_shared_ptr() noexcept
      : ptr_(nullptr)
      , cb_(nullptr)
  {}
  constexpr local_shared_ptr(std::nullptr_t) noexcept
      : ptr_(nullptr)
      , cb_(nullptr)
  {}

  ~local_shared_ptr()
  {
    release();
  }

  local_shared_ptr &operator=(std::nullptr_t) noexcept
  {
    release();
    ptr_ = nullptr;
    cb_ = nullptr;
    return *this;
  }

  local_shared_ptr(const local_shared_ptr &other) noexcept
      : ptr_(other.ptr_)
      , cb_(other.cb_)
  {
    if (cb_)
      cb_->ref_count++;
  }

  // FIX: Constraint now checks conversion between element pointers (int* ->
  // const int*) instead of template arguments (int(*)[] -> const int(*)[])
  template <typename U>
    requires std::convertible_to<
                 typename local_shared_ptr<U>::element_type *,
                 element_type *>
  local_shared_ptr(const local_shared_ptr<U> &other) noexcept
      : ptr_(other.ptr_)
      , cb_(other.cb_)
  {
    if (cb_)
      cb_->ref_count++;
  }

  local_shared_ptr &operator=(const local_shared_ptr &other) noexcept
  {
    if (this != &other) {
      release();
      ptr_ = other.ptr_;
      cb_ = other.cb_;
      if (cb_)
        cb_->ref_count++;
    }
    return *this;
  }

  template <typename U>
    requires std::convertible_to<
        typename local_shared_ptr<U>::element_type *,
        element_type *>
  local_shared_ptr &operator=(const local_shared_ptr<U> &other) noexcept
  {
    // Handle self-assignment via aliasing check or just ref count logic
    // Standard shared_ptr logic: increment first, then decrement old
    auto *new_cb = other.cb_;
    if (new_cb)
      new_cb->ref_count++;

    release();

    ptr_ = other.ptr_;
    cb_ = new_cb;
    return *this;
  }

  local_shared_ptr(local_shared_ptr &&other) noexcept
      : ptr_(other.ptr_)
      , cb_(other.cb_)
  {
    other.ptr_ = nullptr;
    other.cb_ = nullptr;
  }

  // FIX: Constraint updated for Move Constructor as well
  template <typename U>
    requires std::convertible_to<
                 typename local_shared_ptr<U>::element_type *,
                 element_type *>
  local_shared_ptr(local_shared_ptr<U> &&other) noexcept
      : ptr_(other.ptr_)
      , cb_(other.cb_)
  {
    other.ptr_ = nullptr;
    other.cb_ = nullptr;
  }

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

  template <typename U>
    requires std::convertible_to<
        typename local_shared_ptr<U>::element_type *,
        element_type *>
  local_shared_ptr &operator=(local_shared_ptr<U> &&other) noexcept
  {
    release();
    ptr_ = other.ptr_;
    cb_ = other.cb_;
    other.ptr_ = nullptr;
    other.cb_ = nullptr;
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

  // Required for detection by frozen::pointer_traits
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
          // dtor logic would go here
        }
        delete[] reinterpret_cast<char *>(cb_);
      }
    }
  }

  template <typename U, typename A>
  friend local_shared_ptr<U>
  allocate_local_shared_for_overwrite(std::size_t, const A &);

  // Friend other specializations to allow access to .ptr_ and .cb_
  template <typename U> friend class local_shared_ptr;
};

// Factory
template <typename T, typename Alloc>
local_shared_ptr<T>
allocate_local_shared_for_overwrite(std::size_t n, const Alloc &)
{
  using Element = std::remove_extent_t<T>;
  constexpr std::size_t header_size = sizeof(detail::local_control_block_base);
  constexpr std::size_t align = alignof(Element);
  std::size_t padding =
      (header_size % align != 0) ? align - (header_size % align) : 0;
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
