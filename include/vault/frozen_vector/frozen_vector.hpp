#ifndef FROZEN_FROZEN_VECTOR_HPP
#define FROZEN_FROZEN_VECTOR_HPP

#include <compare>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace frozen {

template <typename T, typename Handle = std::shared_ptr<const T[]>>
class frozen_vector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using const_reference = const T &;
  using const_pointer = const T *;
  using const_iterator = const T *;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // Default Constructor
  [[nodiscard]]
  frozen_vector() noexcept
      : data_(nullptr)
      , size_(0)
  {}

  // Internal Constructor
  [[nodiscard]]
  frozen_vector(Handle data, size_type size)
      : data_(std::move(data))
      , size_(size)
  {}

  // Copy Constructor
  [[nodiscard]]
  frozen_vector(const frozen_vector &other)
      : data_(other.data_)
      , size_(other.size_)
  {}

  // Move Constructor
  [[nodiscard]]
  frozen_vector(frozen_vector &&other) noexcept
      : data_(std::move(other.data_))
      , size_(other.size_)
  {
    other.size_ = 0;
  }

  // Assignment
  frozen_vector &operator=(const frozen_vector &other)
  {
    if (this != &other) {
      data_ = other.data_;
      size_ = other.size_;
    }
    return *this;
  }

  frozen_vector &operator=(frozen_vector &&other) noexcept
  {
    if (this != &other) {
      data_ = std::move(other.data_);
      size_ = other.size_;
      other.size_ = 0;
    }
    return *this;
  }

  // Element Access
  [[nodiscard]] const_reference operator[](size_type pos) const
  {
    return data_[pos];
  }

  [[nodiscard]] const_reference at(size_type pos) const
  {
    if (pos >= size_)
      throw std::out_of_range("frozen_vector::at");
    return data_[pos];
  }

  [[nodiscard]] const_reference front() const
  {
    return data_[0];
  }
  [[nodiscard]] const_reference back() const
  {
    return data_[size_ - 1];
  }
  [[nodiscard]] const T *data() const noexcept
  {
    return data_.get();
  }

  // Iterators
  [[nodiscard]] const_iterator begin() const noexcept
  {
    return data_.get();
  }
  [[nodiscard]] const_iterator end() const noexcept
  {
    return data_.get() + size_;
  }
  [[nodiscard]] const_iterator cbegin() const noexcept
  {
    return data_.get();
  }
  [[nodiscard]] const_iterator cend() const noexcept
  {
    return data_.get() + size_;
  }

  // Reverse Iterators
  [[nodiscard]] const_reverse_iterator rbegin() const noexcept
  {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] const_reverse_iterator rend() const noexcept
  {
    return const_reverse_iterator(begin());
  }
  [[nodiscard]] const_reverse_iterator crbegin() const noexcept
  {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] const_reverse_iterator crend() const noexcept
  {
    return const_reverse_iterator(begin());
  }

  // Capacity
  [[nodiscard]] bool empty() const noexcept
  {
    return size_ == 0;
  }
  [[nodiscard]] size_type size() const noexcept
  {
    return size_;
  }
  [[nodiscard]] size_type max_size() const noexcept
  {
    return std::numeric_limits<difference_type>::max();
  }

  // Comparisons
  [[nodiscard]] bool operator==(const frozen_vector &other) const
  {
    return std::equal(begin(), end(), other.begin(), other.end());
  }

  [[nodiscard]] std::strong_ordering operator<=>(const frozen_vector &other
  ) const
  {
    return std::lexicographical_compare_three_way(
        begin(), end(), other.begin(), other.end()
    );
  }

private:
  Handle data_;
  size_type size_;
};

} // namespace frozen

#endif // FROZEN_FROZEN_VECTOR_HPP
