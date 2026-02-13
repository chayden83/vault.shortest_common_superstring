/**
 * @file lazy_flatbuffer.hpp
 * @brief Generic, policy-based, thread-safe lazy FlatBuffer wrapper.
 */
#pragma once

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <vault/frozen_vector/local_shared_ptr.hpp>

// clang-format off

namespace lazyfb {

  // -----------------------------------------------------------------------------
  // Concepts
  // -----------------------------------------------------------------------------

  template <typename T>
  concept FlatBufferTable = std::is_base_of_v<flatbuffers::Table, T>;

  // -----------------------------------------------------------------------------
  // Policies
  // -----------------------------------------------------------------------------

  namespace policies {

    struct ThreadSafe {
      template <typename T> using storage_type = std::shared_ptr<T>;

      using mutex_type = std::shared_mutex;
      using read_lock  = std::shared_lock<mutex_type>;
      using write_lock = std::unique_lock<mutex_type>;

      template <typename T, typename... Args>
      static auto make_storage(Args&&... args)
      {
        return std::make_shared<T>(std::forward<Args>(args)...);
      }
    };

    struct SingleThreaded {
      template <typename T> using storage_type = frozen::local_shared_ptr<T>;

      // No-op mutex
      struct mutex_type {
        void lock         () {}
        void unlock       () {}
        void lock_shared  () {}
        void unlock_shared() {}
      };

      struct read_lock {
        explicit read_lock(mutex_type&) {}
      };

      struct write_lock {
        explicit write_lock(mutex_type&) {}
      };

      template <typename T, typename... Args>
      static auto make_storage(Args&&... args)
      {
        return frozen::local_shared_ptr<T>(new T { std::forward<Args>(args)... });
      }
    };

  } // namespace policies

  // -----------------------------------------------------------------------------
  // Lazy Wrapper Implementation
  // -----------------------------------------------------------------------------

  template <FlatBufferTable T, typename Policy = policies::ThreadSafe>
  class LazyWrapper {
  public:
    using Accessor = const flatbuffers::Vector<uint8_t>* (T::*)() const;

    /**
     * @brief Greedily verifies the current table (envelope) but skips nested
     * fields.
     */
    [[nodiscard]]
    static auto create(const uint8_t* data, size_t size)
      -> std::optional<LazyWrapper<T, Policy>>
    {
      if (!data || size == 0) [[unlikely]] {
        return std::nullopt;
      }

      flatbuffers::Verifier::Options options;
      options.check_nested_flatbuffers = false; // SKIP nested verification

      flatbuffers::Verifier verifier{data, size, options};
      if (verifier.template VerifyBuffer<T>(nullptr)) {
        return LazyWrapper{
          Policy::template make_storage<State>(flatbuffers::GetRoot<T>(data))};
      }
      return std::nullopt;
    }

    /**
     * @brief Access a nested field.
     * Uses linear search on a vector of cached accessors.
     */
    template <FlatBufferTable NestedT>
    [[nodiscard]]
    auto get_nested(Accessor accessor) const
      -> std::optional<LazyWrapper<NestedT, Policy>>
    {
      /* 1. Fast Path: Read Lock + Linear Search */ {
        typename Policy::read_lock lock(state_->mutex);
        for (size_t i = 0; i < state_->accessors.size(); ++i) {
          if (state_->accessors[i] == accessor) {
            return std::make_optional(
              *static_cast<LazyWrapper<NestedT, Policy>*>(state_->cache[i]));
          }
        }
      }

      /* 2. Slow Path: Write Lock + Verify + Insert */ {
        typename Policy::write_lock lock(state_->mutex);

        // Double-check (standard optimization)
        for (size_t i = 0; i < state_->accessors.size(); ++i) {
          if (state_->accessors[i] == accessor) {
            return *static_cast<LazyWrapper<NestedT, Policy>*>(
              state_->cache[i]);
          }
        }

        // Retrieve the raw bytes from the underlying FlatBuffer
        const auto* blob = (state_->root->*accessor)();
        if (!blob) {
          return std::nullopt;
        }

        // Recursively create the wrapper for the nested type
        auto nested =
          LazyWrapper<NestedT, Policy>::create(blob->data(), blob->size());

        if (nested) {
          // Allocate on heap to keep the void* valid
          auto* stored = new LazyWrapper<NestedT, Policy>(*nested);

          state_->accessors.push_back(accessor);
          state_->cache.push_back(static_cast<void*>(stored));

          // Cleanup logic
          state_->cleanup.push_back([stored]() { delete stored; });

          return *nested;
        }
      }

      return std::nullopt;
    }

    [[nodiscard]] auto operator->() const noexcept -> const T*
    {
      return state_->root;
    }

    [[nodiscard]] auto get() const noexcept -> const T* { return state_->root; }

  private:
    struct State {
      explicit State(const T* r)
          : root(r)
      {
        accessors.reserve(4);
        cache.reserve(4);
        cleanup.reserve(4);
      }

      ~State()
      {
        for (auto& fn : cleanup) {
          fn();
        }
      }

      const T*                            root;
      mutable typename Policy::mutex_type mutex;

      // Parallel vectors for SoA (Structure of Arrays) layout
      std::vector<Accessor>              accessors;
      std::vector<void*>                 cache;
      std::vector<std::function<void()>> cleanup;
    };

    explicit LazyWrapper(typename Policy::template storage_type<State> s)
        : state_(std::move(s))
    {}

    typename Policy::template storage_type<State> state_;
  };

} // namespace lazyfb
