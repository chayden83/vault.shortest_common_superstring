#ifndef LAYOUT_ITERATOR_HPP
#define LAYOUT_ITERATOR_HPP

#include <cassert>
#include <iterator>
#include <memory>

#include "utilities.hpp"
#include "vault/flat_map/concepts.hpp"

namespace eytzinger {

  /**
   * @brief Generic bidirectional iterator for policy-based layout maps.
   * * Delegates access to the container via
   * operator[eytzinger::unordered_index].
   * * Traverses via Container::policy_type::next_index / prev_index
   */
  template <typename Container>
    requires ForwardLayoutPolicy<layout_policy_t<Container>>
  class layout_iterator {
  public:
    using iterator_category = std::bidirectional_iterator_tag;

    using value_type      = typename Container::value_type;
    using reference       = typename Container::reference;
    using difference_type = typename Container::difference_type;

    using policy_type = typename Container::policy_type;

    struct proxy_pointer {
      reference ref;

      constexpr const reference* operator->() const noexcept
      {
        return std::addressof(ref);
      }
    };

    using pointer = proxy_pointer;

    constexpr layout_iterator() = default;

    constexpr layout_iterator(
      Container& container, std::ptrdiff_t index) noexcept
        : container_(std::addressof(container))
        , index_(index)
    {
      assert(
        container_ != nullptr && "Iterator must attach to a valid container");
      // Valid indices are 0 to size() (inclusive).
      // size() represents the end sentinel.
      assert(index >= 0
        && index <= static_cast<std::ptrdiff_t>(container_->size())
        && "Index out of bounds");
    }

    [[nodiscard]] constexpr reference operator*() const noexcept
    {
      assert(container_ != nullptr);
      assert(index_ != static_cast<std::ptrdiff_t>(container_->size())
        && "Cannot dereference end iterator");
      assert(index_ >= 0
        && index_ < static_cast<std::ptrdiff_t>(container_->size())
        && "Dereferencing out of bounds index");
      return (*container_)[unordered_index<std::ptrdiff_t>{index_}];
    }

    [[nodiscard]] constexpr pointer operator->() const noexcept
    {
      return {**this};
    }

    constexpr layout_iterator& operator++() noexcept
    {
      assert(container_ != nullptr);
      assert(index_ != static_cast<std::ptrdiff_t>(container_->size())
        && "Cannot increment end iterator");

      index_ = policy_type::next_index(index_, container_->size());
      return *this;
    }

    constexpr layout_iterator& operator--() noexcept
      requires BidirectionalLayoutPolicy<layout_policy_t<Container>>
    {
      assert(container_ != nullptr);
      // Decrementing end() (index == size) is valid and should go to last
      // element. Decrementing begin() is invalid.

      index_ = policy_type::prev_index(index_, container_->size());
      return *this;
    }

    constexpr layout_iterator operator++(int) noexcept
    {
      auto temp = *this;
      ++(*this);
      return temp;
    }

    constexpr layout_iterator operator--(int) noexcept
      requires BidirectionalLayoutPolicy<layout_policy_t<Container>>
    {
      auto temp = *this;
      --(*this);
      return temp;
    }

    constexpr bool operator==(
      const layout_iterator& other) const noexcept = default;

    [[nodiscard]] constexpr std::ptrdiff_t get_index() const noexcept
    {
      return index_;
    }

  private:
    Container* container_ = nullptr;
    // Default to -1 (singular/invalid).
    // Initialized iterators will be in range [0, size].
    std::ptrdiff_t index_ = -1;
  };

} // namespace eytzinger

#endif // LAYOUT_ITERATOR_HPP
