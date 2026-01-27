#ifndef FROZEN_FROZEN_VECTOR_BUILDER_HPP
#define FROZEN_FROZEN_VECTOR_BUILDER_HPP

#include <memory>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <iterator>
#include <concepts>
#include <type_traits>
#include <compare>
#include <limits>

#include "concepts.hpp"
#include "shared_storage_policy.hpp"
#include "frozen_vector.hpp"
#include "traits.hpp" 

namespace frozen {

template <
    typename T, 
    typename ptr_policy = shared_storage_policy<T>,
    typename alloc = std::allocator<T>
>
requires std::is_default_constructible_v<T> && std::is_move_assignable_v<T>
class frozen_vector_builder {
public:
    using value_type = T;
    using allocator_type = alloc;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]]
    explicit frozen_vector_builder(const allocator_type& a = allocator_type()) noexcept
        : allocator_(a), data_(nullptr), size_(0), capacity_(0) {}

    [[nodiscard]]
    explicit frozen_vector_builder(size_type count, const allocator_type& a = allocator_type())
        : allocator_(a), size_(count), capacity_(count) {
        data_ = ptr_policy::allocate(count, allocator_);
        
        // FIX: Replaced uninitialized_value_construct with fill_n.
        // The policy allocates objects that are already alive (default constructed or indeterminate).
        // Calling constructor again is UB. We use assignment to zero/default them.
        if (data_) {
            std::fill_n(data_.get(), count, T());
        }
    }

    // Conditional Copy Constructor
    [[nodiscard]]
    frozen_vector_builder(const frozen_vector_builder& other)
    requires can_copy_handle<ptr_policy, typename ptr_policy::mutable_handle_type, alloc>
        : allocator_(std::allocator_traits<allocator_type>::select_on_container_copy_construction(other.allocator_))
        , size_(other.size_)
        , capacity_(other.size_)
    {
        if (other.size_ > 0) {
            data_ = ptr_policy::copy(other.data_, other.size_, allocator_);
        }
    }

    // Conditional Copy Assignment
    frozen_vector_builder& operator=(const frozen_vector_builder& other)
    requires can_copy_handle<ptr_policy, typename ptr_policy::mutable_handle_type, alloc>
    {
        if (this != &other) {
            if constexpr (std::allocator_traits<allocator_type>::propagate_on_container_copy_assignment::value) {
                if (allocator_ != other.allocator_) {
                    allocator_ = other.allocator_;
                    data_ = nullptr;
                    capacity_ = 0;
                    size_ = 0;
                }
            }

            if (capacity_ >= other.size_) {
                std::copy(other.begin(), other.end(), begin());
                size_ = other.size_;
            } else {
                if (other.size_ > 0) {
                    data_ = ptr_policy::copy(other.data_, other.size_, allocator_);
                } else {
                    data_ = nullptr;
                }
                size_ = other.size_;
                capacity_ = other.size_;
            }
        }
        return *this;
    }

    // Move Constructor
    [[nodiscard]]
    frozen_vector_builder(frozen_vector_builder&& other) noexcept
        : allocator_(std::move(other.allocator_))
        , data_(std::move(other.data_))
        , size_(other.size_)
        , capacity_(other.capacity_) {
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // Allocator-Extended Move Constructor
    [[nodiscard]]
    frozen_vector_builder(frozen_vector_builder&& other, const allocator_type& a)
        : allocator_(a), size_(0), capacity_(0) {
        if (allocator_ == other.allocator_) {
            data_ = std::move(other.data_);
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.size_ = 0;
            other.capacity_ = 0;
        } else {
            if (other.size_ > 0) {
                data_ = ptr_policy::allocate(other.size_, allocator_);
                std::move(other.begin(), other.end(), data_.get());
                size_ = other.size_;
                capacity_ = other.size_;
            }
            other.clear();
        }
    }

    frozen_vector_builder& operator=(frozen_vector_builder&& other) noexcept {
        if (this != &other) {
            if constexpr (std::allocator_traits<allocator_type>::propagate_on_container_move_assignment::value) {
                allocator_ = std::move(other.allocator_);
            }
            data_ = std::move(other.data_);
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    ~frozen_vector_builder() = default;

    [[nodiscard]] allocator_type get_allocator() const noexcept { return allocator_; }

    using DefaultConstHandle = std::conditional_t<
        frozen::pointer_traits<typename ptr_policy::mutable_handle_type>::is_reference_counted,
        typename frozen::pointer_traits<typename ptr_policy::mutable_handle_type>::template rebind<const T[]>,
        std::shared_ptr<const T[]>
    >;

    template <typename ConstHandle = DefaultConstHandle>
    [[nodiscard]] 
    frozen_vector<T, ConstHandle> freeze() && {
        using traits = freeze_traits<typename ptr_policy::mutable_handle_type, ConstHandle>;
        
        auto local_data = std::move(data_);
        
        try {
            auto frozen_ptr = traits::freeze(std::move(local_data));
            
            size_type final_size = size_;
            size_ = 0;
            capacity_ = 0;
            
            return frozen_vector<T, ConstHandle>(std::move(frozen_ptr), final_size);
        }
        catch (...) {
            data_ = std::move(local_data);
            throw;
        }
    }

    template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, T>
    void append_range(R&& rg) {
        if constexpr (std::ranges::sized_range<R>) {
            auto n = std::ranges::size(rg);
            if (n > 0) {
                ensure_capacity(size_ + n);
                std::ranges::copy(rg, begin() + size_);
                size_ += n;
            }
        } else {
            for (auto&& elem : rg) {
                push_back(std::forward<decltype(elem)>(elem));
            }
        }
    }

    void shrink_to_fit() {
        if (size_ < capacity_) {
            auto new_data = ptr_policy::allocate(size_, allocator_);
            if (size_ > 0) {
                std::move(begin(), end(), new_data.get());
            }
            data_ = std::move(new_data);
            capacity_ = size_;
        }
    }

    void push_back(const T& value) {
        ensure_capacity(size_ + 1);
        data_[size_] = value; 
        ++size_;
    }

    void push_back(T&& value) {
        ensure_capacity(size_ + 1);
        data_[size_] = std::move(value);
        ++size_;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args) {
        ensure_capacity(size_ + 1);
        data_[size_] = T(std::forward<Args>(args)...);
        return data_[size_++];
    }

    void pop_back() { if (size_ > 0) --size_; }

    void reserve(size_type new_cap) {
        if (new_cap > capacity_) {
            reallocate(new_cap);
        }
    }

    void resize(size_type count) {
        if (count > size_) {
            if (count > capacity_) reserve(count);
            // FIX: Using fill instead of uninitialized_value_construct
            std::fill(begin() + size_, begin() + count, T());
        }
        size_ = count;
    }

    void clear() noexcept { size_ = 0; }

    [[nodiscard]] reference operator[](size_type pos) { return data_[pos]; }
    [[nodiscard]] const_reference operator[](size_type pos) const { return data_[pos]; }
    
    [[nodiscard]] reference at(size_type pos) {
        if (pos >= size_) throw std::out_of_range("frozen_vector_builder::at");
        return data_[pos];
    }
    [[nodiscard]] const_reference at(size_type pos) const {
        if (pos >= size_) throw std::out_of_range("frozen_vector_builder::at");
        return data_[pos];
    }

    [[nodiscard]] reference front() { return data_[0]; }
    [[nodiscard]] const_reference front() const { return data_[0]; }
    [[nodiscard]] reference back() { return data_[size_ - 1]; }
    [[nodiscard]] const_reference back() const { return data_[size_ - 1]; }
    
    [[nodiscard]] pointer data() noexcept { return data_.get(); }
    [[nodiscard]] const_pointer data() const noexcept { return data_.get(); }

    [[nodiscard]] iterator begin() noexcept { return data_.get(); }
    [[nodiscard]] iterator end() noexcept { return data_.get() + size_; }
    [[nodiscard]] const_iterator begin() const noexcept { return data_.get(); }
    [[nodiscard]] const_iterator end() const noexcept { return data_.get() + size_; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data_.get(); }
    [[nodiscard]] const_iterator cend() const noexcept { return data_.get() + size_; }

    [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_type max_size() const noexcept { return std::numeric_limits<difference_type>::max(); }

    [[nodiscard]] bool operator==(const frozen_vector_builder& other) const {
        return std::equal(begin(), end(), other.begin(), other.end());
    }

    [[nodiscard]] std::strong_ordering operator<=>(const frozen_vector_builder& other) const {
        return std::lexicographical_compare_three_way(begin(), end(), other.begin(), other.end());
    }

private:
    [[no_unique_address]] allocator_type allocator_;
    typename ptr_policy::mutable_handle_type data_;
    size_type size_;
    size_type capacity_;

    void ensure_capacity(size_type min_cap) {
        if (min_cap > capacity_) [[unlikely]] {
            size_type new_cap = capacity_ == 0 ? 1 : capacity_ * 2;
            if (new_cap < min_cap) new_cap = min_cap;
            reallocate(new_cap);
        }
    }

    void reallocate(size_type new_cap) {
        auto new_data = ptr_policy::allocate(new_cap, allocator_);
        if (data_) {
            std::move(begin(), end(), new_data.get());
        }
        data_ = std::move(new_data);
        capacity_ = new_cap;
    }
};

} // namespace frozen

#endif // FROZEN_FROZEN_VECTOR_BUILDER_HPP
