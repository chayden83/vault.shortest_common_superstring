#ifndef LAYOUT_MAP_HPP
#define LAYOUT_MAP_HPP

#include <iterator>
#include <memory>
#include <ranges>
#include <vector>
#include <utility>
#include <concepts>
#include <algorithm>
#include <stdexcept>
#include <initializer_list>

#include "concepts.hpp"
#include "layout_iterator.hpp"
#include "eytzinger_layout_policy.hpp"

// clang-format off

namespace std {
  static constexpr inline struct sorted_unique_t { } const sorted_unique { };
}

/**
 * @brief A generic map container that decouples storage layout from interface.
 * * @tparam LayoutPolicy Controls memory layout, permutation, and search algorithms
 * (e.g., eytzinger_layout_policy, sorted_layout_policy).
 */
template<
    typename K,
    typename V,
    std::strict_weak_order<K, K> Compare = std::less<>,
    typename LayoutPolicy = eytzinger::eytzinger_layout_policy<6>,
    typename Allocator = std::allocator<std::pair<const K, V>>,
    template <typename, typename> typename KeyContainer = std::vector,
    template <typename, typename> typename ValueContainer = std::vector
>
requires eytzinger::ForwardLayoutPolicy<
    LayoutPolicy,
    std::ranges::iterator_t<
        KeyContainer<K, typename std::allocator_traits<Allocator>::template rebind_alloc<K>>
    >,
    Compare
>
class layout_map {
public:
    using key_type        = K;
    using mapped_type     = V;
    using value_type      = std::pair<K, V>;
    using key_compare     = Compare;
    using allocator_type  = Allocator;
    using policy_type     = LayoutPolicy; // Exposed for iterator access
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    using key_allocator_type   = typename std::allocator_traits<Allocator>::template rebind_alloc<K>;
    using value_allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<V>;
    using key_storage_type   = KeyContainer<K, key_allocator_type>;
    using value_storage_type = ValueContainer<V, value_allocator_type>;

    using iterator        = layout_iterator<const layout_map>;
    using const_iterator  = iterator;
    using reference       = std::pair<const key_type&, const mapped_type&>;

private:
    key_storage_type   keys_;
    value_storage_type values_;
    [[no_unique_address]] Compare compare_;

public:
    // --- Allocator Access ---
    [[nodiscard]] allocator_type get_allocator() const noexcept {
        return allocator_type(keys_.get_allocator());
    }

    // --- Constructors ---
    [[nodiscard]] layout_map() : layout_map(Compare(), Allocator()) {}

    [[nodiscard]] explicit layout_map(const Compare& comp, const Allocator& alloc = Allocator())
        : keys_(key_allocator_type(alloc)), values_(value_allocator_type(alloc)), compare_(comp) {}

    [[nodiscard]] explicit layout_map(const Allocator& alloc)
        : layout_map(Compare(), alloc) {}

    [[nodiscard]] layout_map(const layout_map& other) = default;
    [[nodiscard]] layout_map(layout_map&& other) noexcept = default;

    [[nodiscard]] layout_map(const layout_map& other, const Allocator& alloc)
        : keys_(other.keys_, key_allocator_type(alloc))
        , values_(other.values_, value_allocator_type(alloc))
        , compare_(other.compare_) {}

    [[nodiscard]] layout_map(layout_map&& other, const Allocator& alloc)
        : keys_(std::move(other.keys_), key_allocator_type(alloc))
        , values_(std::move(other.values_), value_allocator_type(alloc))
        , compare_(std::move(other.compare_)) {}

    template<std::input_iterator It>
    [[nodiscard]] layout_map(It first, It last, const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : keys_(key_allocator_type(alloc)), values_(value_allocator_type(alloc)), compare_(comp)
    {
        for (; first != last; ++first) {
            keys_.push_back(first->first);
            values_.push_back(first->second);
        }
        sort_and_unique_zipped();
        policy_type::permute(std::views::zip(keys_, values_));
    }

    template<std::input_iterator It>
    [[nodiscard]] layout_map(It first, It last, const Allocator& alloc)
        : layout_map(first, last, Compare(), alloc) {}

    [[nodiscard]] layout_map(std::initializer_list<value_type> init, const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : layout_map(init.begin(), init.end(), comp, alloc) {}

    [[nodiscard]] layout_map(std::initializer_list<value_type> init, const Allocator& alloc)
        : layout_map(init.begin(), init.end(), Compare(), alloc) {}

    template<std::ranges::forward_range R>
    [[nodiscard]] layout_map(std::sorted_unique_t, R&& range, const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : keys_(key_allocator_type(alloc)), values_(value_allocator_type(alloc)), compare_(comp)
    {
        for (auto&& [k, v] : range) {
            keys_.push_back(k);
            values_.push_back(v);
        }
        policy_type::permute(std::views::zip(keys_, values_));
    }

    template<std::ranges::forward_range R>
    [[nodiscard]] layout_map(std::sorted_unique_t tag, R&& range, const Allocator& alloc)
        : layout_map(tag, std::forward<R>(range), Compare(), alloc) {}

    [[nodiscard]] layout_map(std::in_place_t, key_storage_type&& k_cont, value_storage_type&& v_cont,
                  const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : keys_(std::move(k_cont), key_allocator_type(alloc))
        , values_(std::move(v_cont), value_allocator_type(alloc))
        , compare_(comp)
    {
        if (keys_.empty()) return;
        sort_and_unique_zipped();
        policy_type::permute(std::views::zip(keys_, values_));
    }

    [[nodiscard]] layout_map(std::in_place_t tag, key_storage_type&& k_cont, value_storage_type&& v_cont, const Allocator& alloc)
        : layout_map(tag, std::move(k_cont), std::move(v_cont), Compare(), alloc) {}

    // Assignment
    layout_map& operator=(const layout_map&) = default;
    layout_map& operator=(layout_map&&) noexcept = default;
    layout_map& operator=(std::initializer_list<value_type> ilist) {
        layout_map tmp(ilist, compare_, get_allocator());
        *this = std::move(tmp);
        return *this;
    }

    // --- Access via Strong Types ---

    // O(1) access by physical layout index
    template <std::integral I>
    [[nodiscard]] constexpr reference operator[](eytzinger::unordered_index<I> idx) const {
        return { keys_[idx.index_], values_[idx.index_] };
    }

    // Access by sorted rank (complexity depends on LayoutPolicy)
    template <std::integral I>
    [[nodiscard]] constexpr reference operator[](eytzinger::ordered_index<I> idx) const {
        std::size_t phys_idx = policy_type::sorted_rank_to_index(static_cast<std::size_t>(idx.index_), keys_.size());
        return { keys_[phys_idx], values_[phys_idx] };
    }

    // --- Direct Storage Access ---
    [[nodiscard]] const key_storage_type& unordered_keys() const noexcept { return keys_; }
    [[nodiscard]] const value_storage_type& unordered_values() const noexcept { return values_; }
    [[nodiscard]] auto unordered_items() const noexcept { return std::views::zip(keys_, values_); }

    // --- Sorted Views ---
    [[nodiscard]] auto keys() const noexcept {
        using Iter = layout_iterator<const key_storage_type>;
        std::size_t start_idx = keys_.empty() ? -1 : policy_type::sorted_rank_to_index(0, keys_.size());
        return std::ranges::subrange(Iter(keys_, static_cast<std::ptrdiff_t>(start_idx)), Iter(keys_, -1));
    }

    [[nodiscard]] auto values() const noexcept {
        using Iter = layout_iterator<const value_storage_type>;
        std::size_t start_idx = values_.empty() ? -1 : policy_type::sorted_rank_to_index(0, values_.size());
        return std::ranges::subrange(Iter(values_, static_cast<std::ptrdiff_t>(start_idx)), Iter(values_, -1));
    }

    // --- Lookup Interface ---
    template <typename K0 = key_type>
    [[nodiscard]] const_iterator lower_bound(const K0& key) const noexcept {
        if (keys_.empty()) return end();
        auto kit = policy_type::lower_bound(keys_, key, compare_);
        if (kit == keys_.end()) return end();
        auto idx = std::distance(keys_.begin(), kit);
        return const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
    }

    template <typename K0 = key_type>
    [[nodiscard]] const_iterator upper_bound(const K0& key) const noexcept {
        if (keys_.empty()) return end();
        auto kit = policy_type::upper_bound(keys_, key, compare_);
        if (kit == keys_.end()) return end();
        auto idx = std::distance(keys_.begin(), kit);
        return const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
    }

    template <typename K0 = key_type>
    [[nodiscard]] const_iterator find(const K0& key) const noexcept {
        const auto lb = lower_bound(key);
        if (lb != end() && !compare_(key, lb->first)) return lb;
        return end();
    }

    template <typename K0 = key_type>
    [[nodiscard]] bool contains(const K0& key) const noexcept { return find(key) != end(); }

    template <typename K0 = key_type>
    [[nodiscard]] size_type count(const K0& key) const noexcept { return contains(key) ? 1 : 0; }

    template <typename K0 = key_type>
    [[nodiscard]] std::pair<const_iterator, const_iterator> equal_range(const K0& key) const noexcept {
        const auto lb = lower_bound(key);
        if (lb != end() && !compare_(key, lb->first)) {
            auto ub = lb;
            return { lb, ++ub };
        }
        return { lb, lb };
    }

    template <typename K0 = key_type>
    [[nodiscard]] const mapped_type& at(const K0& key) const {
        auto it = find(key);
        if (it == end()) throw std::out_of_range("layout_map::at: key not found");
        return it->second;
    }

    // --- Capacity and Iterators ---
    [[nodiscard]] size_type size() const noexcept { return keys_.size(); }
    [[nodiscard]] bool empty() const noexcept { return keys_.empty(); }

    const_iterator begin() const noexcept {
        if (keys_.empty()) return end();
        std::size_t idx = policy_type::sorted_rank_to_index(0, keys_.size());
        return const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
    }

    const_iterator end() const noexcept { return const_iterator(*this, -1); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

private:
    void sort_and_unique_zipped() {
        auto z = std::views::zip(keys_, values_);
        std::ranges::sort(z, [this](const auto& a, const auto& b) {
            return compare_(std::get<0>(a), std::get<0>(b));
        });
        auto [first_erase, _] = std::ranges::unique(z, [this](const auto& a, const auto& b) {
            const auto& k1 = std::get<0>(a);
            const auto& k2 = std::get<0>(b);
            return !compare_(k1, k2) && !compare_(k2, k1);
        });
        auto new_size = static_cast<size_type>(std::ranges::distance(z.begin(), first_erase));
        keys_.resize(new_size);
        values_.resize(new_size);
    }
};

#endif // LAYOUT_MAP_HPP
