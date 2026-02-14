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

  namespace detail {
    struct local_control_block_base {
      long ref_count{1};
      virtual ~local_control_block_base() = default;
      virtual void release_ref()          = 0;
    };

    template <typename Ptr, typename Deleter>
    struct local_control_block_split : local_control_block_base {
      Ptr                           ptr;
      [[no_unique_address]] Deleter deleter;

      local_control_block_split(Ptr p, Deleter d)
          : ptr(p)
          , deleter(std::move(d))
      {
        assert(ref_count == 1);
      }

      void release_ref() override
      {
        assert(ref_count > 0);
        if (--ref_count == 0) {
          deleter(ptr);
          delete this;
        }
      }
    };

    template <typename T>
    struct local_control_block_inplace : local_control_block_base {
      std::size_t size;

      local_control_block_inplace(std::size_t n)
          : size(n)
      {
        assert(ref_count == 1);
      }

      void release_ref() override
      {
        assert(ref_count > 0);
        if (--ref_count == 0) {
          using Element = std::remove_extent_t<T>;
          if constexpr (!std::is_trivially_destructible_v<Element>) {
            constexpr std::size_t header_size =
              sizeof(local_control_block_inplace<T>);
            constexpr std::size_t align = alignof(Element);
            std::size_t           padding =
              (header_size % align != 0) ? align - (header_size % align) : 0;

            char*    raw_mem = reinterpret_cast<char*>(this);
            Element* data =
              reinterpret_cast<Element*>(raw_mem + header_size + padding);

            for (std::size_t i = 0; i < size; ++i) {
              data[i].~Element();
            }
          }
          delete[] reinterpret_cast<char*>(this);
        }
      }
    };

    template <typename T> struct inplace_storage {
      using Element      = std::remove_extent_t<T>;
      using ControlBlock = local_control_block_inplace<T>;

      static constexpr std::size_t header_size = sizeof(ControlBlock);
      static constexpr std::size_t align       = alignof(Element);
      static constexpr std::size_t padding =
        (header_size % align != 0) ? align - (header_size % align) : 0;

      [[nodiscard]] static char* allocate(std::size_t n)
      {
        std::size_t total_size = header_size + padding + (sizeof(Element) * n);
        return new char[total_size];
      }

      [[nodiscard]] static Element* get_data(char* mem)
      {
        return reinterpret_cast<Element*>(mem + header_size + padding);
      }
    };
  } // namespace detail

  template <typename T> class local_shared_ptr {
  public:
    using element_type = std::remove_extent_t<T>;

    // --- Constructors ---
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
        cb_ = new detail::local_control_block_split<element_type*, Deleter>(
          p, Deleter{});
      } else {
        cb_ = nullptr;
      }
    }

    template <typename U, typename Deleter>
      requires std::convertible_to<U*, element_type*>
    local_shared_ptr(std::unique_ptr<U, Deleter>&& other)
    {
      if (other) {
        ptr_ = other.get();
        cb_  = new detail::local_control_block_split<U*, Deleter>(
          other.release(), std::move(other.get_deleter()));
      } else {
        ptr_ = nullptr;
        cb_  = nullptr;
      }
    }

    ~local_shared_ptr()
    {
      if (cb_) {
        cb_->release_ref();
      }
    }

    // --- Copy / Move ---
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

    // --- Aliasing Constructor ---
    template <typename U>
    local_shared_ptr(const local_shared_ptr<U>& owner, element_type* p) noexcept
        : ptr_(p)
        , cb_(owner.cb_)
    {
      if (cb_) {
        cb_->ref_count++;
      }
    }

    template <typename U>
    local_shared_ptr(local_shared_ptr<U>&& owner, element_type* p) noexcept
        : ptr_(p)
        , cb_(owner.cb_)
    {
      owner.ptr_ = nullptr;
      owner.cb_  = nullptr;
    }

    // --- Assignment ---
    local_shared_ptr& operator=(const local_shared_ptr& other) noexcept
    {
      if (this != &other) {
        if (other.cb_) {
          other.cb_->ref_count++;
        }
        if (cb_) {
          cb_->release_ref();
        }
        ptr_ = other.ptr_;
        cb_  = other.cb_;
      }
      return *this;
    }

    local_shared_ptr& operator=(local_shared_ptr&& other) noexcept
    {
      if (this != &other) {
        if (cb_) {
          cb_->release_ref();
        }
        ptr_       = other.ptr_;
        cb_        = other.cb_;
        other.ptr_ = nullptr;
        other.cb_  = nullptr;
      }
      return *this;
    }

    // --- Modifiers ---
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

    // --- Observers ---
    [[nodiscard]] element_type* get() const noexcept { return ptr_; }

    [[nodiscard]] element_type& operator*() const noexcept
    {
      assert(ptr_);
      return *ptr_;
    }

    [[nodiscard]] element_type* operator->() const noexcept
    {
      assert(ptr_);
      return ptr_;
    }

    [[nodiscard]] element_type& operator[](std::ptrdiff_t i) const noexcept
    {
      assert(ptr_);
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
    [[nodiscard]] bool owner_before(
      const local_shared_ptr<U>& other) const noexcept
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

    // Factories
    template <typename U, typename... Args>
    friend local_shared_ptr<U> make_local_shared(Args&&... args);

    template <typename U, typename Alloc, typename... Args>
    friend local_shared_ptr<U> allocate_local_shared(
      const Alloc& alloc, Args&&... args);

    template <typename U>
    friend local_shared_ptr<U> make_local_shared_for_overwrite();

    template <typename U, typename Alloc>
    friend local_shared_ptr<U> allocate_local_shared_for_overwrite(
      const Alloc& alloc);

    template <typename U>
      requires std::is_array_v<U>
    friend local_shared_ptr<U> make_local_shared_for_overwrite(std::size_t n);

    template <typename U, typename Alloc>
      requires std::is_array_v<U>
    friend local_shared_ptr<U> allocate_local_shared_for_overwrite(
      std::size_t n, const Alloc& alloc);

    template <typename U> friend class local_shared_ptr;
  };

  // --- Comparisons ---
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

  // --- Specialized Swap ---
  template <typename T>
  void swap(local_shared_ptr<T>& lhs, local_shared_ptr<T>& rhs) noexcept
  {
    lhs.swap(rhs);
  }

  // --- Casting ---
  template <typename T, typename U>
  [[nodiscard]] local_shared_ptr<T> static_pointer_cast(
    const local_shared_ptr<U>& r) noexcept
  {
    auto p = static_cast<typename local_shared_ptr<T>::element_type*>(r.get());
    return local_shared_ptr<T>(r, p);
  }

  template <typename T, typename U>
  [[nodiscard]] local_shared_ptr<T> dynamic_pointer_cast(
    const local_shared_ptr<U>& r) noexcept
  {
    if (auto p =
          dynamic_cast<typename local_shared_ptr<T>::element_type*>(r.get())) {
      return local_shared_ptr<T>(r, p);
    }
    return local_shared_ptr<T>();
  }

  template <typename T, typename U>
  [[nodiscard]] local_shared_ptr<T> const_pointer_cast(
    const local_shared_ptr<U>& r) noexcept
  {
    auto p = const_cast<typename local_shared_ptr<T>::element_type*>(r.get());
    return local_shared_ptr<T>(r, p);
  }

  template <typename T, typename U>
  [[nodiscard]] local_shared_ptr<T> reinterpret_pointer_cast(
    const local_shared_ptr<U>& r) noexcept
  {
    auto p =
      reinterpret_cast<typename local_shared_ptr<T>::element_type*>(r.get());
    return local_shared_ptr<T>(r, p);
  }

  // --- Factories Implementation ---

  template <typename T, typename... Args>
    requires(!std::is_array_v<T>)
  [[nodiscard]] local_shared_ptr<T> make_local_shared(Args&&... args)
  {
    using Storage = detail::inplace_storage<T>;
    char* mem     = Storage::allocate(1);
    auto* cb      = new (mem) detail::local_control_block_inplace<T>(1);
    auto* data    = Storage::get_data(mem);

    try {
      std::construct_at(data, std::forward<Args>(args)...);
    } catch (...) {
      cb->~local_control_block_inplace();
      delete[] mem;
      throw;
    }
    return local_shared_ptr<T>(data, cb);
  }

  template <typename T, typename Alloc>
  [[nodiscard]] local_shared_ptr<T> allocate_local_shared_for_overwrite(
    const Alloc&)
  {
    static_assert(
      !std::is_array_v<T>, "Use the array overload for array types");
    using Storage = detail::inplace_storage<T>;
    char* mem     = Storage::allocate(1);
    auto* cb      = new (mem) detail::local_control_block_inplace<T>(1);
    auto* data    = Storage::get_data(mem);

    std::uninitialized_default_construct_n(data, 1);
    return local_shared_ptr<T>(data, cb);
  }

  template <typename T, typename Alloc>
    requires std::is_array_v<T>
  [[nodiscard]] local_shared_ptr<T> allocate_local_shared_for_overwrite(
    std::size_t n, const Alloc&)
  {
    using Storage = detail::inplace_storage<T>;
    char* mem     = Storage::allocate(n);
    auto* cb      = new (mem) detail::local_control_block_inplace<T>(n);
    auto* data    = Storage::get_data(mem);

    std::uninitialized_default_construct_n(data, n);
    return local_shared_ptr<T>(data, cb);
  }

  template <typename T>
  [[nodiscard]] local_shared_ptr<T> make_local_shared_for_overwrite()
  {
    return allocate_local_shared_for_overwrite<T>(std::allocator<void>{});
  }

  template <typename T>
    requires std::is_array_v<T>
  [[nodiscard]] local_shared_ptr<T> make_local_shared_for_overwrite(
    std::size_t n)
  {
    return allocate_local_shared_for_overwrite<T>(n, std::allocator<void>{});
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

#endif // FROZEN_LOCAL_SHARED_PTR_HPP
