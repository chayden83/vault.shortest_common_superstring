#ifndef LAYOUT_MAP_HPP
#define LAYOUT_MAP_HPP

#include <algorithm>
#include <cassert>
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <vault/algorithm/amac.hpp>

#include "concepts.hpp"
#include "eytzinger_layout_policy.hpp"
#include "layout_iterator.hpp"

namespace std {
  static constexpr inline struct sorted_unique_t {
  } const sorted_unique{};
} // namespace std

namespace eytzinger {

  template <typename K,
    typename V,
    std::strict_weak_order<K, K> Compare = std::less<>,
    typename LayoutPolicy                = eytzinger_layout_policy<6>,
    typename Allocator = std::allocator<std::pair<const K, V>>,
    template <typename, typename> typename KeyContainer   = std::vector,
    template <typename, typename> typename ValueContainer = std::vector>
    requires OrderedForwardLayoutPolicy<LayoutPolicy,
               std::ranges::iterator_t<const KeyContainer<K,
                 typename std::allocator_traits<
                   Allocator>::template rebind_alloc<K>>>,
               Compare>
    && std::ranges::random_access_range<ValueContainer<V,
      typename std::allocator_traits<Allocator>::template rebind_alloc<V>>>
  class layout_map {
  public:
    using key_type        = K;
    using mapped_type     = V;
    using value_type      = std::pair<K, V>;
    using allocator_type  = Allocator;
    using policy_type     = LayoutPolicy;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare     = Compare;

    using key_allocator_type =
      typename std::allocator_traits<Allocator>::template rebind_alloc<K>;
    using value_allocator_type =
      typename std::allocator_traits<Allocator>::template rebind_alloc<V>;
    using key_storage_type   = KeyContainer<K, key_allocator_type>;
    using value_storage_type = ValueContainer<V, value_allocator_type>;

    using iterator               = layout_iterator<const layout_map>;
    using const_iterator         = layout_iterator<const layout_map>;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using reference = std::pair<const key_type&, const mapped_type&>;

  private:
    key_storage_type              keys_;
    value_storage_type            values_;
    [[no_unique_address]] Compare compare_;

  public:
    [[nodiscard]] constexpr allocator_type get_allocator() const noexcept
    {
      return allocator_type(keys_.get_allocator());
    }

    [[nodiscard]] constexpr layout_map()
        : layout_map(Compare(), Allocator())
    {}

    [[nodiscard]] constexpr explicit layout_map(
      const Compare& comp, const Allocator& alloc = Allocator())
        : keys_(key_allocator_type(alloc))
        , values_(value_allocator_type(alloc))
        , compare_(comp)
    {
      assert(keys_.empty());
    }

    [[nodiscard]] constexpr explicit layout_map(const Allocator& alloc)
        : layout_map(Compare(), alloc)
    {}

    [[nodiscard]] constexpr layout_map(const layout_map& other)     = default;
    [[nodiscard]] constexpr layout_map(layout_map&& other) noexcept = default;

    [[nodiscard]] constexpr layout_map(
      const layout_map& other, const Allocator& alloc)
        : keys_(other.keys_, key_allocator_type(alloc))
        , values_(other.values_, value_allocator_type(alloc))
        , compare_(other.compare_)
    {
      assert(
        keys_.size() == values_.size() && "Inconsistent copy construction");
    }

    [[nodiscard]] constexpr layout_map(
      layout_map&& other, const Allocator& alloc)
        : keys_(std::move(other.keys_), key_allocator_type(alloc))
        , values_(std::move(other.values_), value_allocator_type(alloc))
        , compare_(std::move(other.compare_))
    {
      assert(
        keys_.size() == values_.size() && "Inconsistent move construction");
    }

    template <std::input_iterator It>
    [[nodiscard]] constexpr layout_map(It first,
      It                                  last,
      const Compare&                      comp  = Compare(),
      const Allocator&                    alloc = Allocator())
        : keys_(key_allocator_type(alloc))
        , values_(value_allocator_type(alloc))
        , compare_(comp)
    {
      for (; first != last; ++first) {
        keys_.push_back(first->first);
        values_.push_back(first->second);
      }
      assert(keys_.size() == values_.size());
      sort_and_unique_zipped();
      policy_type::permute(std::views::zip(keys_, values_));
      assert(keys_.size() == values_.size());
    }

    template <std::input_iterator It>
    [[nodiscard]] constexpr layout_map(
      It first, It last, const Allocator& alloc)
        : layout_map(first, last, Compare(), alloc)
    {}

    [[nodiscard]] constexpr layout_map(std::initializer_list<value_type> init,
      const Compare&   comp  = Compare(),
      const Allocator& alloc = Allocator())
        : layout_map(init.begin(), init.end(), comp, alloc)
    {}

    [[nodiscard]] constexpr layout_map(
      std::initializer_list<value_type> init, const Allocator& alloc)
        : layout_map(init.begin(), init.end(), Compare(), alloc)
    {}

    template <std::ranges::forward_range R>
    [[nodiscard]] constexpr layout_map(std::sorted_unique_t,
      R&&              range,
      const Compare&   comp  = Compare(),
      const Allocator& alloc = Allocator())
        : keys_(key_allocator_type(alloc))
        , values_(value_allocator_type(alloc))
        , compare_(comp)
    {
      for (auto&& [k, v] : range) {
        keys_.push_back(k);
        values_.push_back(v);
      }
      assert(keys_.size() == values_.size());
      policy_type::permute(std::views::zip(keys_, values_));
    }

    template <std::ranges::forward_range R>
    [[nodiscard]] constexpr layout_map(
      std::sorted_unique_t tag, R&& range, const Allocator& alloc)
        : layout_map(tag, std::forward<R>(range), Compare(), alloc)
    {}

    [[nodiscard]] constexpr layout_map(std::in_place_t,
      key_storage_type&&   k_cont,
      value_storage_type&& v_cont,
      const Compare&       comp  = Compare(),
      const Allocator&     alloc = Allocator())
        : keys_(std::move(k_cont), key_allocator_type(alloc))
        , values_(std::move(v_cont), value_allocator_type(alloc))
        , compare_(comp)
    {
      if (keys_.size() != values_.size()) {
        throw std::invalid_argument(
          "layout_map: key and value containers must have same size");
      }
      sort_and_unique_zipped();
      policy_type::permute(std::views::zip(keys_, values_));
      assert(keys_.size() == values_.size());
    }

    [[nodiscard]] constexpr layout_map(std::in_place_t,
      key_storage_type&&   k_cont,
      value_storage_type&& v_cont,
      const Allocator&     alloc)
        : layout_map(std::in_place,
            std::move(k_cont),
            std::move(v_cont),
            Compare(),
            alloc)
    {}

    constexpr layout_map& operator=(const layout_map&)     = default;
    constexpr layout_map& operator=(layout_map&&) noexcept = default;

    constexpr layout_map& operator=(std::initializer_list<value_type> ilist)
    {
      layout_map tmp(ilist, compare_, get_allocator());
      *this = std::move(tmp);
      return *this;
    }

    template <std::integral I>
    [[nodiscard]] constexpr reference operator[](unordered_index<I> idx) const
    {
      assert(idx.index_ >= 0 && static_cast<size_t>(idx.index_) < keys_.size()
        && "Index out of bounds");
      return {keys_[idx.index_], values_[idx.index_]};
    }

    template <std::integral I>
    [[nodiscard]] constexpr reference operator[](ordered_index<I> idx) const
    {
      assert(idx.index_ >= 0 && static_cast<size_t>(idx.index_) < keys_.size()
        && "Rank out of bounds");
      std::size_t phys_idx = policy_type::sorted_rank_to_index(
        static_cast<std::size_t>(idx.index_), keys_.size());
      assert(phys_idx < keys_.size());
      return {keys_[phys_idx], values_[phys_idx]};
    }

    [[nodiscard]] constexpr const key_storage_type&
    unordered_keys() const noexcept
    {
      return keys_;
    }

    [[nodiscard]] constexpr const value_storage_type&
    unordered_values() const noexcept
    {
      assert(keys_.size() == values_.size());
      return values_;
    }

    [[nodiscard]] constexpr auto unordered_items() const noexcept
      -> decltype(std::views::zip(keys_, values_))
    {
      assert(keys_.size() == values_.size());
      return std::views::zip(keys_, values_);
    }

    [[nodiscard]] constexpr auto keys() const noexcept
    {
      using Iter = layout_iterator<const key_storage_type>;
      std::size_t start_idx =
        keys_.empty() ? -1 : policy_type::sorted_rank_to_index(0, keys_.size());
      return std::ranges::subrange(
        Iter(keys_, static_cast<std::ptrdiff_t>(start_idx)), Iter(keys_, -1));
    }

    [[nodiscard]] constexpr auto values() const noexcept
    {
      using Iter            = layout_iterator<const value_storage_type>;
      std::size_t start_idx = values_.empty()
        ? -1
        : policy_type::sorted_rank_to_index(0, values_.size());
      return std::ranges::subrange(
        Iter(values_, static_cast<std::ptrdiff_t>(start_idx)),
        Iter(values_, -1));
    }

    template <typename K0 = key_type>
    [[nodiscard]] constexpr const_iterator lower_bound(
      const K0& key) const noexcept
    {
      if (keys_.empty()) {
        return end();
      }
      auto kit = policy_type::lower_bound(keys_, key, compare_);
      if (kit == keys_.end()) {
        return end();
      }
      auto idx = std::distance(keys_.begin(), kit);
      return const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
    }

    template <typename K0 = key_type>
    [[nodiscard]] constexpr const_iterator upper_bound(
      const K0& key) const noexcept
    {
      if (keys_.empty()) {
        return end();
      }
      auto kit = policy_type::upper_bound(keys_, key, compare_);
      if (kit == keys_.end()) {
        return end();
      }
      auto idx = std::distance(keys_.begin(), kit);
      return const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
    }

    template <typename K0 = key_type>
    [[nodiscard]] constexpr const_iterator find(const K0& key) const noexcept
    {
      const auto lb = lower_bound(key);
      if (lb != end() && !compare_(key, lb->first)) {
        return lb;
      }
      return end();
    }

    template <typename K0 = key_type>
    [[nodiscard]] constexpr bool contains(const K0& key) const noexcept
    {
      return find(key) != end();
    }

    template <typename K0 = key_type>
    [[nodiscard]] constexpr size_type count(const K0& key) const noexcept
    {
      return contains(key) ? 1 : 0;
    }

    template <typename K0 = key_type>
    [[nodiscard]] constexpr std::pair<const_iterator, const_iterator>
    equal_range(const K0& key) const noexcept
    {
      const auto lb = lower_bound(key);
      if (lb != end() && !compare_(key, lb->first)) {
        auto ub = lb;
        return {lb, ++ub};
      }
      return {lb, lb};
    }

    template <typename K0 = key_type>
    [[nodiscard]] constexpr const mapped_type& at(const K0& key) const
    {
      auto it = find(key);
      if (it == end()) {
        throw std::out_of_range("layout_map::at: key not found");
      }
      return it->second;
    }

    // --- AMAC Batch Interface ---

    template <uint8_t          BatchSize = 16,
      std::ranges::input_range Needles,
      typename OutputIt>
      requires std::output_iterator<OutputIt,
        std::pair<
          std::ranges::iterator_t<const std::remove_reference_t<Needles>>,
          const_iterator>>
    void batch_lower_bound(Needles&& needles, OutputIt output) const
    {
      if (empty()) {
        for (auto it = std::ranges::begin(needles);
          it != std::ranges::end(needles);
          ++it) {
          *output++ = {it, end()};
        }
        return;
      }

      using NeedleIter =
        std::ranges::iterator_t<const std::remove_reference_t<Needles>>;

      struct Job {
        decltype(policy_type::batch_lower_bound.make_job(
          std::declval<const key_storage_type&>().cbegin(),
          0,
          std::declval<key_type>(),
          std::declval<Compare>())) impl;
        NeedleIter                  needle_it;

        Job(const layout_map& map, NeedleIter it)
            : impl(policy_type::batch_lower_bound.make_job(
                map.keys_.cbegin(), map.keys_.size(), *it, map.compare_))
            , needle_it(it)
        {}

        Job(Job&&)            = default;
        Job& operator=(Job&&) = default;

        vault::amac::concepts::job_step_result auto init()
        {
          return impl.init();
        }

        vault::amac::concepts::job_step_result auto step()
        {
          return impl.step();
        }
      };

      auto factory = [](const layout_map& map, NeedleIter it) {
        return Job(map, it);
      };

      auto reporter = [this, &output](Job&& job) {
        auto           key_it = job.impl.result();
        const_iterator result_it;
        if (key_it == keys_.end()) {
          result_it = end();
        } else {
          auto idx  = std::distance(keys_.cbegin(), key_it);
          result_it = const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
        }
        *output++ = {job.needle_it, result_it};
      };

      vault::amac::coordinator<BatchSize>(*this, needles, factory, reporter);
    }

    template <uint8_t          BatchSize = 16,
      std::ranges::input_range Needles,
      typename OutputIt>
      requires std::output_iterator<OutputIt,
        std::pair<
          std::ranges::iterator_t<const std::remove_reference_t<Needles>>,
          const_iterator>>
    void batch_upper_bound(Needles&& needles, OutputIt output) const
    {
      if (empty()) {
        for (auto it = std::ranges::begin(needles);
          it != std::ranges::end(needles);
          ++it) {
          *output++ = {it, end()};
        }
        return;
      }

      using NeedleIter =
        std::ranges::iterator_t<const std::remove_reference_t<Needles>>;

      struct Job {
        decltype(policy_type::batch_upper_bound.make_job(
          std::declval<const key_storage_type&>().cbegin(),
          0,
          std::declval<key_type>(),
          std::declval<Compare>())) impl;
        NeedleIter                  needle_it;

        Job(const layout_map& map, NeedleIter it)
            : impl(policy_type::batch_upper_bound.make_job(
                map.keys_.cbegin(), map.keys_.size(), *it, map.compare_))
            , needle_it(it)
        {}

        Job(Job&&)            = default;
        Job& operator=(Job&&) = default;

        vault::amac::concepts::job_step_result auto init()
        {
          return impl.init();
        }

        vault::amac::concepts::job_step_result auto step()
        {
          return impl.step();
        }
      };

      auto factory = [](const layout_map& map, NeedleIter it) {
        return Job(map, it);
      };

      auto reporter = [this, &output](Job&& job) {
        auto           key_it = job.impl.result();
        const_iterator result_it;
        if (key_it == keys_.end()) {
          result_it = end();
        } else {
          auto idx  = std::distance(keys_.cbegin(), key_it);
          result_it = const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
        }
        *output++ = {job.needle_it, result_it};
      };

      vault::amac::coordinator<BatchSize>(*this, needles, factory, reporter);
    }

    template <uint8_t          BatchSize = 16,
      std::ranges::input_range Needles,
      typename OutputIt>
      requires std::output_iterator<OutputIt,
        std::pair<
          std::ranges::iterator_t<const std::remove_reference_t<Needles>>,
          const_iterator>>
    void batch_find(Needles&& needles, OutputIt output) const
    {
      if (empty()) {
        for (auto it = std::ranges::begin(needles);
          it != std::ranges::end(needles);
          ++it) {
          *output++ = {it, end()};
        }
        return;
      }

      using NeedleIter =
        std::ranges::iterator_t<const std::remove_reference_t<Needles>>;

      struct Job {
        decltype(policy_type::batch_lower_bound.make_job(
          std::declval<const key_storage_type&>().cbegin(),
          0,
          std::declval<key_type>(),
          std::declval<Compare>())) impl;
        NeedleIter                  needle_it;

        Job(const layout_map& map, NeedleIter it)
            : impl(policy_type::batch_lower_bound.make_job(
                map.keys_.cbegin(), map.keys_.size(), *it, map.compare_))
            , needle_it(it)
        {}

        Job(Job&&)            = default;
        Job& operator=(Job&&) = default;

        vault::amac::concepts::job_step_result auto init()
        {
          return impl.init();
        }

        vault::amac::concepts::job_step_result auto step()
        {
          return impl.step();
        }
      };

      auto factory = [](const layout_map& map, NeedleIter it) {
        return Job(map, it);
      };

      auto reporter = [this, &output](Job&& job) {
        auto           key_it    = job.impl.result();
        const_iterator result_it = end();

        if (key_it != keys_.end()) {
          if (!compare_(*job.needle_it, *key_it)) {
            auto idx  = std::distance(keys_.cbegin(), key_it);
            result_it = const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
          }
        }
        *output++ = {job.needle_it, result_it};
      };

      vault::amac::coordinator<BatchSize>(*this, needles, factory, reporter);
    }

    [[nodiscard]] constexpr size_type size() const noexcept
    {
      assert(keys_.size() == values_.size());
      return keys_.size();
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
      assert(keys_.size() == values_.size());
      return keys_.empty();
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept
    {
      if (keys_.empty()) {
        return end();
      }
      std::size_t idx = policy_type::sorted_rank_to_index(0, keys_.size());
      assert(idx < keys_.size());
      return const_iterator(*this, static_cast<std::ptrdiff_t>(idx));
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept
    {
      return const_iterator(*this, -1);
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept
    {
      return begin();
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept
    {
      return end();
    }

    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept
      requires std::bidirectional_iterator<const_reverse_iterator>
    {
      return const_reverse_iterator{end()};
    }

    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept
      requires std::bidirectional_iterator<const_reverse_iterator>
    {
      return const_reverse_iterator{begin()};
    }

    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept
      requires std::bidirectional_iterator<const_reverse_iterator>
    {
      return rbegin();
    }

    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept
      requires std::bidirectional_iterator<const_reverse_iterator>
    {
      return rend();
    }

  private:
    constexpr void sort_and_unique_zipped()
    {
      assert(keys_.size() == values_.size());
      auto z = std::views::zip(keys_, values_);
      std::ranges::sort(z, [this](const auto& a, const auto& b) {
        return compare_(std::get<0>(a), std::get<0>(b));
      });
      auto [first_erase, _] =
        std::ranges::unique(z, [this](const auto& a, const auto& b) {
          const auto& k1 = std::get<0>(a);
          const auto& k2 = std::get<0>(b);
          return !compare_(k1, k2) && !compare_(k2, k1);
        });
      auto new_size =
        static_cast<size_type>(std::ranges::distance(z.begin(), first_erase));
      keys_.resize(new_size);
      values_.resize(new_size);
      assert(keys_.size() == values_.size());
    }
  };

} // namespace eytzinger

#endif // LAYOUT_MAP_HPP
