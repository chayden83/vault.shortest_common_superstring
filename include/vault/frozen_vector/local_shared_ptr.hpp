#ifndef FROZEN_LOCAL_SHARED_PTR_HPP
#define FROZEN_LOCAL_SHARED_PTR_HPP

#include <cassert>
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
        assert(ref_count == 1 && "New control block must have ref_count 1");
      }

      void release_ref() override
      {
        assert(ref_count > 0 && "Double free detected: ref_count is already 0");
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
        assert(ref_count == 1 && "New control block must have ref_count 1");
      }

      void release_ref() override
      {
        assert(ref_count > 0 && "Double free detected: ref_count is already 0");
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
  } // namespace detail

  template <typename T, typename Alloc>
  local_shared_ptr<T>
  allocate_local_shared_for_overwrite(std::size_t n, const Alloc& alloc);

  template <typename T> class local_shared_ptr {
  public:
    using element_type = std::remove_extent_t<T>;

    // Constructors
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
            p, Deleter{}
        );
        assert(cb_->ref_count == 1);
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
        assert(
            ptr_ != nullptr && "unique_ptr was valid but get() returned null"
        );
        cb_ = new detail::local_control_block_split<U*, Deleter>(
            other.release(), std::move(other.get_deleter())
        );
        assert(cb_->ref_count == 1);
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

    // Copy / Move
    local_shared_ptr(const local_shared_ptr& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      if (cb_) {
        assert(cb_->ref_count > 0 && "Copying from dead pointer");
        cb_->ref_count++;
      }
    }

    template <typename U>
      requires std::convertible_to<
                   typename local_shared_ptr<U>::element_type*,
                   element_type*>
    local_shared_ptr(const local_shared_ptr<U>& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      if (cb_) {
        assert(cb_->ref_count > 0 && "Copying from dead pointer");
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
      requires std::convertible_to<
                   typename local_shared_ptr<U>::element_type*,
                   element_type*>
    local_shared_ptr(local_shared_ptr<U>&& other) noexcept
        : ptr_(other.ptr_)
        , cb_(other.cb_)
    {
      other.ptr_ = nullptr;
      other.cb_  = nullptr;
    }

    // Assignment
    local_shared_ptr& operator=(std::nullptr_t) noexcept
    {
      reset();
      return *this;
    }

    local_shared_ptr& operator=(const local_shared_ptr& other) noexcept
    {
      if (this != &other) {
        if (other.cb_) {
          assert(other.cb_->ref_count > 0);
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
      assert(this != &other && "Self-move assignment is suspicious");
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

    // Aliasing
    template <typename U>
    local_shared_ptr(const local_shared_ptr<U>& owner, element_type* p) noexcept
        : ptr_(p)
        , cb_(owner.cb_)
    {
      if (cb_) {
        assert(cb_->ref_count > 0);
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

    // Operators
    element_type& operator[](std::ptrdiff_t i) const noexcept
    {
      assert(ptr_ != nullptr && "Dereferencing null pointer");
      return ptr_[i];
    }

    element_type& operator*() const noexcept
    {
      assert(ptr_ != nullptr && "Dereferencing null pointer");
      return *ptr_;
    }

    element_type* operator->() const noexcept
    {
      assert(ptr_ != nullptr && "Dereferencing null pointer");
      return ptr_;
    }

    element_type* get() const noexcept { return ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    long use_count() const noexcept
    {
      if (!cb_) {
        return 0;
      }
      assert(cb_->ref_count > 0 && "Corrupted ref count");
      return cb_->ref_count;
    }

    void reset()
    {
      if (cb_) {
        cb_->release_ref();
      }
      ptr_ = nullptr;
      cb_  = nullptr;
    }

  private:
    element_type*                     ptr_;
    detail::local_control_block_base* cb_;
    template <typename U, typename A>
    friend local_shared_ptr<U>
    allocate_local_shared_for_overwrite(std::size_t, const A&);
    template <typename U> friend class local_shared_ptr;
  };

  // Factory
  template <typename T, typename Alloc>
  local_shared_ptr<T>
  allocate_local_shared_for_overwrite(std::size_t n, const Alloc&)
  {
    using Element      = std::remove_extent_t<T>;
    using ControlBlock = detail::local_control_block_inplace<T>;

    constexpr std::size_t header_size = sizeof(ControlBlock);
    constexpr std::size_t align       = alignof(Element);
    std::size_t           padding =
        (header_size % align != 0) ? align - (header_size % align) : 0;
    std::size_t total_size = header_size + padding + (sizeof(Element) * n);

    char* mem = new char[total_size];

    struct Guard {
      char* p;

      ~Guard() { delete[] p; }
    } guard{mem};

    auto*    cb       = new (mem) ControlBlock(n);
    Element* data_ptr = reinterpret_cast<Element*>(mem + header_size + padding);

    try {
      std::uninitialized_default_construct_n(data_ptr, n);
    } catch (...) {
      cb->~ControlBlock();
      throw;
    }

    guard.p = nullptr;

    local_shared_ptr<T> ret;
    ret.cb_  = cb;
    ret.ptr_ = data_ptr;

    assert(ret.use_count() == 1);
    assert(ret.get() != nullptr);

    return ret;
  }

} // namespace frozen

#endif // FROZEN_LOCAL_SHARED_PTR_HPP
