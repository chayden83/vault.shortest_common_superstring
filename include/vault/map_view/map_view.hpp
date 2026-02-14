#ifndef MAP_VIEW_HPP
#define MAP_VIEW_HPP

#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

namespace lib {

  template <typename V> struct insert_result {
    V*   ptr;
    bool inserted;
  };

  namespace concepts {

    /**
     * @brief Constraint for containers compatible with map_view.
     * * RELAXED REQUIREMENT: We only STRICTLY require .find(), .size(), and
     * .empty().
     * * Operations like .at(), .contains(), and .count() can be polyfilled via
     * .find()
     * * in the VTable shim if the container doesn't provide them for the
     * ViewKey.
     */
    template <typename Container, typename ViewKey, typename Value>
    concept map_compatible = requires(Container& c, const ViewKey& key) {
      // Must support finding the key and getting an iterator/pointer back
      { c.find(key) } -> std::same_as<typename Container::iterator>;

      // Check for mapped value accessibility (simulated)
      requires requires(typename Container::iterator it) {
        { it->second } -> std::convertible_to<Value&>;
      };

      // Capacity
      { c.size() } -> std::convertible_to<std::size_t>;
      { c.empty() } -> std::same_as<bool>;
    };

    template <typename Container, typename Key, typename Value>
    concept mutable_map_compatible =
      requires(Container& c, const Key& key, Value&& val) {
        typename Container::mapped_type;
        { c.find(key) };
        { c.size() } -> std::convertible_to<std::size_t>;
        { c.empty() } -> std::same_as<bool>;
        c.clear();
        { c.erase(key) } -> std::convertible_to<std::size_t>;
        { c.insert_or_assign(key, std::forward<Value>(val)) };
      };

  } // namespace concepts

  // =============================================================================
  // map_view
  // =============================================================================

  template <typename ViewKey, typename Value> class [[nodiscard]] map_view {
  public:
    using key_type    = ViewKey;
    using mapped_type = Value;
    using size_type   = std::size_t;

    template <concepts::map_compatible<ViewKey, Value> Container>
    constexpr map_view(Container& container) noexcept
        : container_ptr_{std::addressof(container)}
        , vtable_ptr_{&vtable_storage<Container>}
    {}

    [[nodiscard]] auto at(const ViewKey& key) const -> Value&
    {
      return vtable_ptr_->at(container_ptr_, key);
    }

    [[nodiscard]] auto contains(const ViewKey& key) const -> bool
    {
      return vtable_ptr_->contains(container_ptr_, key);
    }

    [[nodiscard]] auto find(const ViewKey& key) const -> Value*
    {
      return vtable_ptr_->find(container_ptr_, key);
    }

    [[nodiscard]] auto count(const ViewKey& key) const -> size_type
    {
      return vtable_ptr_->count(container_ptr_, key);
    }

    [[nodiscard]] auto size() const noexcept -> size_type
    {
      return vtable_ptr_->size(container_ptr_);
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
      return vtable_ptr_->empty(container_ptr_);
    }

  private:
    struct vtable {
      Value& (*at)(void*, const ViewKey&);
      bool (*contains)(void*, const ViewKey&);
      Value* (*find)(void*, const ViewKey&);
      size_type (*count)(void*, const ViewKey&);
      size_type (*size)(void*) noexcept;
      bool (*empty)(void*) noexcept;
    };

    template <typename Container>
    static constexpr auto vtable_storage =
      vtable{.at = [](void* ptr, const ViewKey& key) -> Value& {
               auto& c = *static_cast<Container*>(ptr);
               // Polyfill: Use find() because c.at() likely doesn't support
               // ViewKey (heterogeneous)
               auto it = c.find(key);
               if (it == c.end()) {
                 throw std::out_of_range("map_view::at - key not found");
               }
               return it->second;
             },
        .contains = [](void* ptr, const ViewKey& key) -> bool {
          auto& c = *static_cast<Container*>(ptr);
          // Polyfill: Use find() to support pre-C++20 or heterogeneous missing
          // overloads
          return c.find(key) != c.end();
        },
        .find = [](void* ptr, const ViewKey& key) -> Value* {
          auto& c  = *static_cast<Container*>(ptr);
          auto  it = c.find(key);
          return (it != c.end()) ? std::addressof(it->second) : nullptr;
        },
        .count = [](void* ptr, const ViewKey& key) -> size_type {
          auto& c = *static_cast<Container*>(ptr);
          return (c.find(key) != c.end()) ? 1 : 0;
        },
        .size = [](void* ptr) noexcept -> size_type {
          return static_cast<Container*>(ptr)->size();
        },
        .empty = [](void* ptr) noexcept -> bool {
          return static_cast<Container*>(ptr)->empty();
        }};

    void*         container_ptr_{nullptr};
    const vtable* vtable_ptr_{nullptr};
  };

  // =============================================================================
  // mutable_map_view
  // =============================================================================

  template <typename StoredKey, typename Value>
  class [[nodiscard]] mutable_map_view {
  public:
    using key_type    = StoredKey;
    using mapped_type = Value;
    using size_type   = std::size_t;

    template <concepts::mutable_map_compatible<StoredKey, Value> Container>
    constexpr mutable_map_view(Container& container) noexcept
        : container_ptr_{std::addressof(container)}
        , vtable_ptr_{&vtable_storage<Container>}
    {}

    [[nodiscard]] auto at(const StoredKey& key) const -> Value&
    {
      return vtable_ptr_->at(container_ptr_, key);
    }

    [[nodiscard]] auto contains(const StoredKey& key) const -> bool
    {
      return vtable_ptr_->contains(container_ptr_, key);
    }

    [[nodiscard]] auto find(const StoredKey& key) const -> Value*
    {
      return vtable_ptr_->find(container_ptr_, key);
    }

    [[nodiscard]] auto size() const noexcept -> size_type
    {
      return vtable_ptr_->size(container_ptr_);
    }

    [[nodiscard]] auto empty() const noexcept -> bool
    {
      return vtable_ptr_->empty(container_ptr_);
    }

    [[nodiscard]] auto max_size() const noexcept -> size_type
    {
      return vtable_ptr_->max_size(container_ptr_);
    }

    auto clear() const noexcept -> void { vtable_ptr_->clear(container_ptr_); }

    auto erase(const StoredKey& key) const -> size_type
    {
      return vtable_ptr_->erase(container_ptr_, key);
    }

    auto insert_or_assign(const StoredKey& key, Value&& val) const
      -> insert_result<Value>
    {
      return vtable_ptr_->insert_or_assign(container_ptr_, key, std::move(val));
    }

    auto try_emplace(const StoredKey& key, Value&& val) const
      -> insert_result<Value>
    {
      return vtable_ptr_->try_emplace(container_ptr_, key, std::move(val));
    }

  private:
    struct vtable {
      Value& (*at)(void*, const StoredKey&);
      bool (*contains)(void*, const StoredKey&);
      Value* (*find)(void*, const StoredKey&);

      bool (*empty)(void*) noexcept;
      size_type (*size)(void*) noexcept;
      size_type (*max_size)(void*) noexcept;

      void (*clear)(void*) noexcept;
      size_type (*erase)(void*, const StoredKey&);
      insert_result<Value> (*insert_or_assign)(
        void*, const StoredKey&, Value&&);
      insert_result<Value> (*try_emplace)(void*, const StoredKey&, Value&&);
    };

    template <typename Container>
    static constexpr auto vtable_storage =
      vtable{.at = [](void* ptr, const StoredKey& key) -> Value& {
               return static_cast<Container*>(ptr)->at(key);
             },
        .contains = [](void* ptr, const StoredKey& key) -> bool {
          return static_cast<Container*>(ptr)->contains(key);
        },
        .find = [](void* ptr, const StoredKey& key) -> Value* {
          auto& c  = *static_cast<Container*>(ptr);
          auto  it = c.find(key);
          return (it != c.end()) ? std::addressof(it->second) : nullptr;
        },
        .empty = [](void* ptr) noexcept -> bool {
          return static_cast<Container*>(ptr)->empty();
        },
        .size = [](void* ptr) noexcept -> size_type {
          return static_cast<Container*>(ptr)->size();
        },
        .max_size = [](void* ptr) noexcept -> size_type {
          return static_cast<Container*>(ptr)->max_size();
        },
        .clear = [](void* ptr) noexcept -> void {
          static_cast<Container*>(ptr)->clear();
        },
        .erase = [](void* ptr, const StoredKey& key) -> size_type {
          return static_cast<Container*>(ptr)->erase(key);
        },
        .insert_or_assign = [](void*           ptr,
                              const StoredKey& key,
                              Value&&          val) -> insert_result<Value> {
          auto [it, inserted] =
            static_cast<Container*>(ptr)->insert_or_assign(key, std::move(val));
          return {std::addressof(it->second), inserted};
        },
        .try_emplace = [](void*           ptr,
                         const StoredKey& key,
                         Value&&          val) -> insert_result<Value> {
          auto [it, inserted] =
            static_cast<Container*>(ptr)->try_emplace(key, std::move(val));
          return {std::addressof(it->second), inserted};
        }};

    void*         container_ptr_{nullptr};
    const vtable* vtable_ptr_{nullptr};
  };

} // namespace lib

#endif // MAP_VIEW_HPP
