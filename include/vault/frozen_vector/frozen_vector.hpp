#ifndef FROZEN_FROZEN_VECTOR_HPP
#define FROZEN_FROZEN_VECTOR_HPP

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include "traits.hpp"

namespace frozen {

  template <typename T, typename Handle = std::shared_ptr<const T[]>>
  class frozen_vector {
  public:
    using value_type             = T;
    using size_type              = std::size_t;
    using difference_type        = std::ptrdiff_t;
    using reference              = const T&;
    using const_reference        = const T&;
    using pointer                = const T*;
    using const_pointer          = const T*;
    using iterator               = const T*;
    using const_iterator         = const T*;
    using reverse_iterator       = std::reverse_iterator<const_iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // ADDED: Public alias for introspection
    using handle_type = Handle;

    // Constructors
    constexpr frozen_vector() noexcept
        : data_(nullptr)
        , size_(0)
    {}

    frozen_vector(Handle data, size_type size) noexcept
        : data_(std::move(data))
        , size_(size)
    {}

    // Iterators
    [[nodiscard]] const_iterator begin() const noexcept
    {
      return get_raw_ptr();
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
      return get_raw_ptr() + size_;
    }

    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }

    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] reverse_iterator rbegin() const noexcept
    {
      return reverse_iterator(end());
    }

    [[nodiscard]] reverse_iterator rend() const noexcept
    {
      return reverse_iterator(begin());
    }

    [[nodiscard]] const_reverse_iterator crbegin() const noexcept
    {
      return rbegin();
    }

    [[nodiscard]] const_reverse_iterator crend() const noexcept
    {
      return rend();
    }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] size_type size() const noexcept { return size_; }

    // Element Access
    [[nodiscard]] const_reference operator[](size_type pos) const
    {
      return get_raw_ptr()[pos];
    }

    [[nodiscard]] const_reference at(size_type pos) const
    {
      if (pos >= size_) {
        throw std::out_of_range("frozen_vector::at");
      }
      return get_raw_ptr()[pos];
    }

    [[nodiscard]] const_reference front() const { return get_raw_ptr()[0]; }

    [[nodiscard]] const_reference back() const
    {
      return get_raw_ptr()[size_ - 1];
    }

    [[nodiscard]] const_pointer data() const noexcept { return get_raw_ptr(); }

  private:
    Handle    data_;
    size_type size_;

    const T* get_raw_ptr() const noexcept { return data_.get(); }
  };

} // namespace frozen

#endif // FROZEN_FROZEN_VECTOR_HPP
