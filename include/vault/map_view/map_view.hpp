#ifndef VAULT_MAP_VIEW_HPP
#define VAULT_MAP_VIEW_HPP

#include <concepts>
#include <cstddef>
#include <memory>      // For std::addressof
#include <stdexcept>   // For std::out_of_range
#include <type_traits> // For std::forward, std::move
#include <utility>     // For std::pair

namespace lib {

  // =============================================================================
  // Result Types
  // =============================================================================

  /**
   * @brief Result structure for insertion operations.
   * * Uses raw pointers to avoid exposing container iterators.
   */
  template <typename V> struct insert_result {
    V*   ptr;      ///< Pointer to the inserted or existing value.
    bool inserted; ///< True if a new element was inserted.
  };

  // =============================================================================
  // Concepts
  // =============================================================================

  namespace concepts {

    /**
     * @brief Minimal requirements for a read-only map view.
     * * We only strictly require .find(), .size(), and .empty().
     * * Other operations (.contains, .at) are polyfilled at compile-time if
     * missing.
     */
    template <typename Container, typename ViewKey, typename Value>
    concept map_compatible = requires(Container& c, const ViewKey& key) {
      // Must support finding the key
      { c.find(key) } -> std::same_as<typename Container::iterator>;

      // Iterator must yield the value type
      requires requires(typename Container::iterator it) {
        { it->second } -> std::convertible_to<Value&>;
      };

      // Capacity
      { c.size() } -> std::convertible_to<std::size_t>;
      { c.empty() } -> std::same_as<bool>;
    };

    /**
     * @brief Requirements for a mutable map view.
     */
    template <typename Container, typename Key, typename Value>
    concept mutable_map_compatible =
      requires(Container& c, const Key& key, Value&& val) {
        typename Container::mapped_type;
        // Lookup
        { c.find(key) };

        // Capacity
        { c.size() } -> std::convertible_to<std::size_t>;
        { c.empty() } -> std::same_as<bool>;

        // Mutation
        c.clear();
        { c.erase(key) } -> std::convertible_to<std::size_t>;
        { c.insert_or_assign(key, std::forward<Value>(val)) };
      };

  } // namespace concepts

  // =============================================================================
  // map_view (Read-Only, Heterogeneous Optimized)
  // =============================================================================

  /**
   * @brief A non-owning, zero-allocation view for looking up values.
   * *
   * * Performance Note: This class uses compile-time detection to use the
   * * container's native .contains() or .at() if available for the ViewKey.
   * * Otherwise, it generates an optimal fallback using .find().
   * *
   * * @tparam ViewKey The type used for lookup (e.g., std::string_view).
   * * @tparam Value The value type (can be const or non-const).
   */
  template <typename ViewKey, typename Value> class [[nodiscard]] map_view {
  public:
    using key_type    = ViewKey;
    using mapped_type = Value;
    using size_type   = std::size_t;

    /**
     * @brief Constructs a view from any compatible container.
     */
    template <concepts::map_compatible<ViewKey, Value> Container>
    constexpr explicit map_view(Container& container) noexcept
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
               // Compile-time branch: Use native .at() if available and
               // strictly correct
               if constexpr (requires {
                               { c.at(key) } -> std::convertible_to<Value&>;
                             }) {
                 return c.at(key);
               } else {
                 auto it = c.find(key);
                 if (it == c.end()) {
                   throw std::out_of_range("map_view::at - key not found");
                 }
                 return it->second;
               }
             },
        .contains = [](void* ptr, const ViewKey& key) -> bool {
          auto& c = *static_cast<Container*>(ptr);
          // Compile-time branch: Use native .contains() (C++20) if available
          if constexpr (requires { c.contains(key); }) {
            return c.contains(key);
          } else {
            return c.find(key) != c.end();
          }
        },
        .find = [](void* ptr, const ViewKey& key) -> Value* {
          auto& c  = *static_cast<Container*>(ptr);
          auto  it = c.find(key);
          return (it != c.end()) ? std::addressof(it->second) : nullptr;
        },
        .count = [](void* ptr, const ViewKey& key) -> size_type {
          auto& c = *static_cast<Container*>(ptr);
          // Compile-time branch: Use native .count() if available
          if constexpr (requires { c.count(key); }) {
            return c.count(key);
          } else {
            return (c.find(key) != c.end()) ? 1 : 0;
          }
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
  // mutable_map_view (Read-Write, Mutation Support)
  // =============================================================================

  /**
   * @brief A non-owning view that supports insertion, erasure, and lookup.
   * * @tparam StoredKey The concrete key type stored in the map (e.g.
   * std::string).
   * * @tparam Value The value type.
   */
  template <typename StoredKey, typename Value>
  class [[nodiscard]] mutable_map_view {
  public:
    using key_type    = StoredKey;
    using mapped_type = Value;
    using size_type   = std::size_t;

    template <concepts::mutable_map_compatible<StoredKey, Value> Container>
    constexpr explicit mutable_map_view(Container& container) noexcept
        : container_ptr_{std::addressof(container)}
        , vtable_ptr_{&vtable_storage<Container>}
    {}

    // --- Lookup ---

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

    // --- Capacity ---

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

    // --- Mutation ---

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
    static constexpr auto vtable_storage = vtable{
      .at = [](void* ptr, const StoredKey& key) -> Value& {
        auto& c = *static_cast<Container*>(ptr);
        // Polyfill .at() for mutation view as well
        if constexpr (requires {
                        { c.at(key) } -> std::convertible_to<Value&>;
                      }) {
          return c.at(key);
        } else {
          auto it = c.find(key);
          if (it == c.end()) {
            throw std::out_of_range("mutable_map_view::at - key not found");
          }
          return it->second;
        }
      },
      .contains = [](void* ptr, const StoredKey& key) -> bool {
        auto& c = *static_cast<Container*>(ptr);
        if constexpr (requires { c.contains(key); }) {
          return c.contains(key);
        } else {
          return c.find(key) != c.end();
        }
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
        if constexpr (requires { static_cast<Container*>(ptr)->max_size(); }) {
          return static_cast<Container*>(ptr)->max_size();
        } else {
          return size_type(-1); // Polyfill for containers lacking max_size
        }
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
        // Check for try_emplace support, otherwise fallback to emplace/insert
        // check
        if constexpr (requires {
                        static_cast<Container*>(ptr)->try_emplace(
                          key, std::move(val));
                      }) {
          auto [it, inserted] =
            static_cast<Container*>(ptr)->try_emplace(key, std::move(val));
          return {std::addressof(it->second), inserted};
        } else {
          // Fallback: Check existence then insert (less efficient, but
          // functional)
          auto& c  = *static_cast<Container*>(ptr);
          auto  it = c.find(key);
          if (it != c.end()) {
            return {std::addressof(it->second), false};
          }
          auto [ins_it, inserted] = c.emplace(key, std::move(val));
          return {std::addressof(ins_it->second), inserted};
        }
      }};

    void*         container_ptr_{nullptr};
    const vtable* vtable_ptr_{nullptr};
  };

} // namespace lib

#endif // VAULT_MAP_VIEW_HPP
