#ifndef LAYOUT_ITERATOR_HPP
#define LAYOUT_ITERATOR_HPP

#include "utilities.hpp"
#include <iterator>
#include <concepts>
#include <memory>
#include <utility>

/**
 * @brief Generic bidirectional iterator for policy-based layout maps.
 * * Delegates access to the container via operator[eytzinger::unordered_index].
 * * Traverses via Container::policy_type::next_index / prev_index
 */
template <typename Container>
class layout_iterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    
    // Fix: Break circular dependency by using nested types directly 
    // instead of std::ranges traits which require the container to be complete.
    using value_type        = typename Container::value_type;
    using reference         = typename Container::reference;
    using difference_type   = typename Container::difference_type;
    
    // Access the policy type from the container to find traversal algorithms
    using policy_type       = typename Container::policy_type;

    // Proxy for operator-> needed because we don't store a std::pair<K,V>
    // but construct a pair of references on the fly.
    struct proxy_pointer {
        reference ref;
        constexpr const reference* operator->() const noexcept { return std::addressof(ref); }
    };
    using pointer = proxy_pointer;

    constexpr layout_iterator(Container& container, std::ptrdiff_t index) noexcept
        : container_(std::addressof(container)), index_(index) {}

    [[nodiscard]] constexpr reference operator*() const noexcept { 
        return (*container_)[eytzinger::unordered_index<std::ptrdiff_t>{index_}]; 
    }

    [[nodiscard]] constexpr pointer operator->() const noexcept { 
        return { **this }; 
    }

    constexpr layout_iterator& operator++() noexcept {
        index_ = policy_type::next_index(index_, container_->size());
        return *this;
    }

    constexpr layout_iterator& operator--() noexcept {
        index_ = policy_type::prev_index(index_, container_->size());
        return *this;
    }

    constexpr layout_iterator operator++(int) noexcept { auto temp = *this; ++(*this); return temp; }
    constexpr layout_iterator operator--(int) noexcept { auto temp = *this; --(*this); return temp; }
    constexpr bool operator==(const layout_iterator& other) const noexcept = default;

    [[nodiscard]] constexpr std::ptrdiff_t get_index() const noexcept { return index_; }

private:
    Container* container_;
    std::ptrdiff_t index_; 
};

#endif // LAYOUT_ITERATOR_HPP
