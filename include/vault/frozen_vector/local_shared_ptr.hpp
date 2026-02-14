#ifndef FROZEN_LOCAL_SHARED_PTR_HPP
#define FROZEN_LOCAL_SHARED_PTR_HPP

#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "traits.hpp"

namespace frozen {

  template <typename T> class local_shared_ptr;
  template <typename T> class local_weak_ptr;

  namespace detail {
    struct local_control_block_base {
      long ref_count{1};
      long weak_count{1};

      virtual ~local_control_block_base()    = default;
      virtual void on_zero_shared() noexcept = 0;
      virtual void on_zero_weak() noexcept   = 0;

      void release_ref() noexcept
      {
        if (--ref_count == 0) {
          on_zero_shared();
          release_weak();
        }
      }

      void release_weak() noexcept
      {
        if (--weak_count == 0) {
          on_zero_weak();
        }
      }
    };

    template <typename Ptr, typename Deleter, typename Alloc>
    struct local_control_block_split final : local_control_block_base {
      Ptr                           ptr;
      [[no_unique_address]] Deleter deleter;
      [[no_unique_address]] Alloc   allocator;

      local_control_block_split(Ptr p, Deleter d, Alloc a)
          : ptr(p)
          , deleter(std::move(d))
          , allocator(std::move(a))
      {}

      void on_zero_shared() noexcept override { deleter(ptr); }

      void on_zero_weak() noexcept override
      {
        using CBAlloc = typename std::allocator_traits<
          Alloc>::template rebind_alloc<local_control_block_split>;
        CBAlloc cb_alloc(allocator);
        std::allocator_traits<CBAlloc>::destroy(cb_alloc, this);
        std::allocator_traits<CBAlloc>::deallocate(cb_alloc, this, 1);
      }
    };

    template <typename T, typename Alloc>
    struct local_control_block_inplace final : local_control_block_base {
      using Element = std::remove_extent_t<T>;
      std::size_t                 size;
      [[no_unique_address]] Alloc allocator;

      local_control_block_inplace(std::size_t n, Alloc a)
          : size(n)
          , allocator(std::move(a))
      {}

      static constexpr std::size_t header_size =
        sizeof(local_control_block_inplace<T, Alloc>);
      static constexpr std::size_t align = alignof(Element);
      static constexpr std::size_t padding =
        (header_size % align != 0) ? align - (header_size % align) : 0;

      void on_zero_shared() noexcept override
      {
        if constexpr (!std::is_trivially_destructible_v<Element>) {
          char*    raw_mem = reinterpret_cast<char*>(this);
          Element* data =
            reinterpret_cast<Element*>(raw_mem + header_size + padding);
          for (std::size_t i = 0; i < size; ++i) {
            std::allocator_traits<Alloc>::destroy(allocator, &data[i]);
          }
        }
      }

      void on_zero_weak() noexcept override
      {
        using RawAlloc =
          typename std::allocator_traits<Alloc>::template rebind_alloc<char>;
        RawAlloc    raw_alloc(allocator);
        std::size_t total_size =
          header_size + padding + (sizeof(Element) * size);
        std::allocator_traits<RawAlloc>::deallocate(
          raw_alloc, reinterpret_cast<char*>(this), total_size);
      }
    };
  } // namespace detail

  // --- Forward Declarations ---
  template <typename T, typename Alloc, typename... Args>
    requires(!std::is_array_v<T>)
  local_shared_ptr<T> allocate_local_shared(Alloc alloc, Args&&... args);

  template <typename T, typename... Args>
    requires(!std::is_array_v<T>)
  local_shared_ptr<T> make_local_shared(Args&&... args);

  template <typename T, typename Alloc>
  local_shared_ptr<T> allocate_local_shared_for_overwrite(Alloc alloc);

  template <typename T, typename Alloc>
    requires std::is_array_v<T>
  local_shared_ptr<T> allocate_local_shared_for_overwrite(
    std::size_t n, Alloc alloc);

  // --- local_weak_ptr ---
  template <typename T> class local_weak_ptr {
  public:
    using element_type = std::remove_extent_t<T>;

    constexpr local_weak_ptr() noexcept
        : ptr_(nullptr)
        , cb_(nullptr)
    {}

    template <typename U>
      requires std::convertible_to<typename local_shared_ptr<U>::element_type*,
                 element_type*>
    local_weak_ptr(const local_shared_ptr<U>& other) noexcept
        : ptr_(other.get())
        , cb_(other.cb_)
    {
      if (cb_) {
        cb_->weak_count++;
      }
    }

    local_weak_ptr(const local_weak_ptr& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      if (cb_) {
        cb_->weak_count++;
      }
    }

    template <typename U>
      requires std::convertible_to<typename local_shared_ptr<U>::element_type*,
                 element_type*>
    local_weak_ptr(const local_weak_ptr<U>& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      if (cb_) {
        cb_->weak_count++;
      }
    }

    local_weak_ptr(local_weak_ptr&& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      other.ptr_ = nullptr;
      other.cb_  = nullptr;
    }

    ~local_weak_ptr()
    {
      if (cb_) {
        cb_->release_weak();
      }
    }

    local_weak_ptr& operator=(const local_weak_ptr& r) noexcept
    {
      local_weak_ptr(r).swap(*this);
      return *this;
    }

    void swap(local_weak_ptr& r) noexcept
    {
      std::swap(ptr_, r.ptr_);
      std::swap(cb_, r.cb_);
    }

    [[nodiscard]] long use_count() const noexcept
    {
      return cb_ ? cb_->ref_count : 0;
    }

    [[nodiscard]] bool expired() const noexcept { return use_count() == 0; }

    [[nodiscard]] local_shared_ptr<T> lock() const noexcept
    {
      return expired() ? local_shared_ptr<T>() : local_shared_ptr<T>(*this);
    }

  private:
    element_type*                     ptr_;
    detail::local_control_block_base* cb_;
    friend class local_shared_ptr<T>;
  };

  // --- local_shared_ptr ---
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

    explicit local_shared_ptr(element_type* p)
        : ptr_(p)
    {
      if (p) {
        using Deleter = std::default_delete<T>;
        using Alloc   = std::allocator<element_type>;
        cb_ =
          new detail::local_control_block_split<element_type*, Deleter, Alloc>(
            p, Deleter{}, Alloc{});
      }
    }

    template <typename U, typename Deleter>
      requires std::convertible_to<U*, element_type*>
    local_shared_ptr(std::unique_ptr<U, Deleter>&& other)
    {
      if (other) {
        ptr_ = other.get();
        cb_  = new detail::local_control_block_split<U*,
           Deleter,
           std::allocator<void>>(other.release(),
          std::move(other.get_deleter()),
          std::allocator<void>{});
      } else {
        ptr_ = nullptr;
        cb_  = nullptr;
      }
    }

    template <typename U>
    explicit local_shared_ptr(const local_weak_ptr<U>& r)
        : ptr_(r.ptr_)
        , cb_(r.cb_)
    {
      if (cb_) {
        if (cb_->ref_count == 0) {
          throw std::bad_weak_ptr();
        }
        cb_->ref_count++;
      }
    }

    ~local_shared_ptr()
    {
      if (cb_) {
        cb_->release_ref();
      }
    }

    local_shared_ptr(const local_shared_ptr& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      if (cb_) {
        cb_->ref_count++;
      }
    }

    template <typename U>
      requires std::convertible_to<typename local_shared_ptr<U>::element_type*,
                 element_type*>
    local_shared_ptr(const local_shared_ptr<U>& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      if (cb_) {
        cb_->ref_count++;
      }
    }

    local_shared_ptr(local_shared_ptr&& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      other.ptr_ = nullptr;
      other.cb_  = nullptr;
    }

    template <typename U>
      requires std::convertible_to<typename local_shared_ptr<U>::element_type*,
                 element_type*>
    local_shared_ptr(local_shared_ptr<U>&& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      other.ptr_ = nullptr;
      other.cb_  = nullptr;
    }

    template <typename U>
    local_shared_ptr(const local_shared_ptr<U>& owner, element_type* p) noexcept
        : ptr_(p)
        , cb_(owner.cb_)
    {
      if (cb_) {
        cb_->ref_count++;
      }
    }

    local_shared_ptr& operator=(const local_shared_ptr& r) noexcept
    {
      local_shared_ptr(r).swap(*this);
      return *this;
    }

    local_shared_ptr& operator=(local_shared_ptr&& r) noexcept
    {
      local_shared_ptr(std::move(r)).swap(*this);
      return *this;
    }

    void swap(local_shared_ptr& other) noexcept
    {
      std::swap(ptr_, other.ptr_);
      std::swap(cb_, other.cb_);
    }

    void reset() noexcept
    {
      if (cb_) {
        cb_->release_ref();
      }
      ptr_ = nullptr;
      cb_  = nullptr;
    }

    [[nodiscard]] element_type* release() noexcept
    {
      element_type* tmp = ptr_;
      ptr_              = nullptr;
      if (cb_) {
        cb_->release_ref();
        cb_ = nullptr;
      }
      return tmp;
    }

    [[nodiscard]] element_type* get() const noexcept { return ptr_; }

    [[nodiscard]] element_type& operator*() const noexcept { return *ptr_; }

    [[nodiscard]] element_type* operator->() const noexcept { return ptr_; }

    [[nodiscard]] element_type& operator[](std::ptrdiff_t i) const noexcept
    {
      assert(ptr_ != nullptr);
      return ptr_[i];
    }

    [[nodiscard]] long use_count() const noexcept
    {
      return cb_ ? cb_->ref_count : 0;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
      return ptr_ != nullptr;
    }

    template <typename U>
    bool owner_before(const local_shared_ptr<U>& other) const noexcept
    {
      return std::less<detail::local_control_block_base*>()(cb_, other.cb_);
    }

  private:
    local_shared_ptr(
      element_type* p, detail::local_control_block_base* cb) noexcept
        : ptr_(p)
        , cb_(cb)
    {}

    element_type*                     ptr_;
    detail::local_control_block_base* cb_;

    template <typename U, typename Alloc, typename... Args>
      requires(!std::is_array_v<U>)
    friend local_shared_ptr<U> allocate_local_shared(
      Alloc alloc, Args&&... args);

    template <typename U, typename... Args>
      requires(!std::is_array_v<U>)
    friend local_shared_ptr<U> make_local_shared(Args&&... args);

    template <typename U, typename Alloc>
    friend local_shared_ptr<U> allocate_local_shared_for_overwrite(Alloc alloc);

    template <typename U, typename Alloc>
      requires std::is_array_v<U>
    friend local_shared_ptr<U> allocate_local_shared_for_overwrite(
      std::size_t n, Alloc alloc);

    friend class local_weak_ptr<T>;
    template <typename U> friend class local_shared_ptr;
  };

  // --- Casting & Non-members ---

  template <typename T, typename U>
  [[nodiscard]] local_shared_ptr<T> static_pointer_cast(
    const local_shared_ptr<U>& r) noexcept
  {
    return local_shared_ptr<T>(
      r, static_cast<typename local_shared_ptr<T>::element_type*>(r.get()));
  }

  template <typename T, typename U>
  bool operator==(
    const local_shared_ptr<T>& lhs, const local_shared_ptr<U>& rhs) noexcept
  {
    return lhs.get() == rhs.get();
  }

  template <typename T>
  bool operator==(const local_shared_ptr<T>& lhs, std::nullptr_t) noexcept
  {
    return lhs.get() == nullptr;
  }

  template <typename T, typename U>
  auto operator<=>(
    const local_shared_ptr<T>& lhs, const local_shared_ptr<U>& rhs) noexcept
  {
    return std::compare_three_way{}(lhs.get(), rhs.get());
  }

  template <typename T>
  void swap(local_shared_ptr<T>& lhs, local_shared_ptr<T>& rhs) noexcept
  {
    lhs.swap(rhs);
  }

  // --- Factories Implementation ---

  template <typename T, typename Alloc, typename... Args>
    requires(!std::is_array_v<T>)
  local_shared_ptr<T> allocate_local_shared(Alloc alloc, Args&&... args)
  {
    using Element = std::remove_extent_t<T>;
    using ObjAlloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<Element>;
    using CB = detail::local_control_block_inplace<T, ObjAlloc>;
    using RawAlloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<char>;

    ObjAlloc o_alloc(alloc);
    RawAlloc r_alloc(alloc);

    std::size_t total_size = CB::header_size + CB::padding + sizeof(Element);
    char* mem = std::allocator_traits<RawAlloc>::allocate(r_alloc, total_size);

    auto* cb = new (mem) CB(1, o_alloc);
    auto* data =
      reinterpret_cast<Element*>(mem + CB::header_size + CB::padding);

    try {
      std::allocator_traits<ObjAlloc>::construct(
        o_alloc, data, std::forward<Args>(args)...);
    } catch (...) {
      cb->~CB();
      std::allocator_traits<RawAlloc>::deallocate(r_alloc, mem, total_size);
      throw;
    }
    return local_shared_ptr<T>(data, cb);
  }

  template <typename T, typename... Args>
    requires(!std::is_array_v<T>)
  local_shared_ptr<T> make_local_shared(Args&&... args)
  {
    return allocate_local_shared<T>(
      std::allocator<std::remove_extent_t<T>>{}, std::forward<Args>(args)...);
  }

  template <typename T, typename Alloc>
    requires std::is_array_v<T>
  local_shared_ptr<T> allocate_local_shared_for_overwrite(
    std::size_t n, Alloc alloc)
  {
    using Element = std::remove_extent_t<T>;
    using ObjAlloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<Element>;
    using CB = detail::local_control_block_inplace<T, ObjAlloc>;
    using RawAlloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<char>;

    ObjAlloc o_alloc(alloc);
    RawAlloc r_alloc(alloc);

    std::size_t total_size =
      CB::header_size + CB::padding + (sizeof(Element) * n);
    char* mem = std::allocator_traits<RawAlloc>::allocate(r_alloc, total_size);

    auto* cb = new (mem) CB(n, o_alloc);
    auto* data =
      reinterpret_cast<Element*>(mem + CB::header_size + CB::padding);

    std::uninitialized_default_construct_n(data, n);
    return local_shared_ptr<T>(data, cb);
  }

  template <typename T, typename Alloc>
  local_shared_ptr<T> allocate_local_shared_for_overwrite(Alloc alloc)
  {
    return allocate_local_shared_for_overwrite<T>(1, alloc);
  }

  template <typename T> local_shared_ptr<T> make_local_shared_for_overwrite()
  {
    return allocate_local_shared_for_overwrite<T>(
      std::allocator<std::remove_extent_t<T>>{});
  }

  template <typename T>
    requires std::is_array_v<T>
  local_shared_ptr<T> make_local_shared_for_overwrite(std::size_t n)
  {
    return allocate_local_shared_for_overwrite<T>(
      n, std::allocator<std::remove_extent_t<T>>{});
  }

} // namespace frozen

namespace std {
  template <typename T> struct hash<frozen::local_shared_ptr<T>> {
    size_t operator()(const frozen::local_shared_ptr<T>& p) const noexcept
    {
      return hash<typename frozen::local_shared_ptr<T>::element_type*>{}(
        p.get());
    }
  };
} // namespace std

#endif
