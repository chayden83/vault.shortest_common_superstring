#ifndef LAYOUT_ITERATOR_HPP
#define LAYOUT_ITERATOR_HPP

#include <cassert> // Added for assertions
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

    // Fix: Break circular dependency by using nested types directly
    // instead of std::ranges traits which require the container to be complete.
    using value_type      = typename Container::value_type;
    using reference       = typename Container::reference;
    using difference_type = typename Container::difference_type;

    // Access the policy type from the container to find traversal algorithms
    using policy_type = typename Container::policy_type;

    // Proxy for operator-> needed because we don't store a std::pair<K,V>
    // but construct a pair of references on the fly.
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
        Container& container, std::ptrdiff_t index
    ) noexcept
        : container_(std::addressof(container))
        , index_(index)
    {
      assert(
          container_ != nullptr && "Iterator must attach to a valid container"
      );
      // -1 is the "end" sentinel; 0 to size-1 are valid data indices
      assert(
          index >= -1 &&
          index <= static_cast<std::ptrdiff_t>(container_->size()) &&
          "Index out of bounds"
      );
    }

    [[nodiscard]] constexpr reference operator*() const noexcept
    {
      assert(container_ != nullptr);
      assert(index_ != -1 && "Cannot dereference end iterator");
      assert(
          index_ >= 0 &&
          index_ < static_cast<std::ptrdiff_t>(container_->size()) &&
          "Dereferencing out of bounds index"
      );
      return (*container_)[unordered_index<std::ptrdiff_t>{index_}];
    }

    [[nodiscard]] constexpr pointer operator->() const noexcept
    {
      return {**this};
    }

    constexpr layout_iterator& operator++() noexcept
    {
      assert(container_ != nullptr);
      assert(index_ != -1 && "Cannot increment end iterator");

      index_ = policy_type::next_index(index_, container_->size());
      return *this;
    }

    constexpr layout_iterator& operator--() noexcept
      requires BidirectionalLayoutPolicy<layout_policy_t<Container>>
    {
      assert(container_ != nullptr);
      // It is valid to decrement the end iterator (-1) to get the last element.
      // It is NOT valid to decrement the begin iterator (usually index 0, or
      // whatever rank 0 maps to). Since we don't know exactly which index maps
      // to rank 0 without asking the policy, we rely on prev_index to return -1
      // if we step backward from begin. However, check that we aren't already
      // at a state that can't be decremented if the policy defines one.

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

    constexpr bool operator==(const layout_iterator& other
    ) const noexcept = default;

    [[nodiscard]] constexpr std::ptrdiff_t get_index() const noexcept
    {
      return index_;
    }

  private:
    Container*     container_ = nullptr;
    std::ptrdiff_t index_     = -1;
  };

} // namespace eytzinger

#endif // LAYOUT_ITERATOR_HPP
