/**
 * @file flatbuffers.hpp
 * @brief Policy-based lazy FlatBuffer wrapper with fixed concepts and traits.
 */
#pragma once

#include <bit>
#include <concepts>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>

namespace vault::fb::concepts {
  template <typename T>
  concept table = std::is_base_of_v<flatbuffers::Table, T>;

  template <typename P, typename T>
  concept history = requires {
    typename P::mutex_type;
    typename P::read_lock;
    typename P::write_lock;

    { P::template make_context<T>() } -> std::same_as<typename P::template storage_type<T>>;
  };
} // namespace vault::fb::concepts

namespace vault::fb::traits {
  template <auto Accessor>
  struct nested_type;
}

namespace vault::fb {
  using accessor_id = std::array<std::byte, 16>;

  struct history_mt_policy {
    using mutex_type = std::shared_mutex;

    template <typename T>
    using storage_type = std::shared_ptr<T>;

    using read_lock  = std::shared_lock<mutex_type>;
    using write_lock = std::unique_lock<mutex_type>;

    template <typename Context, typename... Args>
    static auto make_context(Args&&... args) -> storage_type<Context> {
      return std::make_shared<Context>(std::forward<Args>(args)...);
    }
  };

  struct history_st_policy {
    struct mutex_type {
      constexpr void lock() const noexcept {}

      constexpr void unlock() const noexcept {}

      constexpr void lock_shared() const noexcept {}

      constexpr void unlock_shared() const noexcept {}
    };

    template <typename T>
    using storage_type = boost::local_shared_ptr<T>;

    struct read_lock {
      explicit constexpr read_lock(mutex_type&) noexcept {}
    };

    struct write_lock {
      explicit constexpr write_lock(mutex_type&) noexcept {}
    };

    template <typename Context, typename... Args>
    static auto make_context(Args&&... args) -> storage_type<Context> {
      return boost::make_local_shared<Context>(std::forward<Args>(args)...);
    }
  };

  namespace detail {
    template <typename MutexType>
    struct history {
      using key_type = std::pair<const uint8_t*, accessor_id>;

      std::vector<key_type> cache;
      mutable MutexType     mutex;
    };

    template <auto Value>
    constexpr inline auto const lvalue = std::remove_cvref_t<decltype(Value)>{Value};

    template <auto Accessor>
    inline auto const id = std::bit_cast<accessor_id>(lvalue<Accessor>);

    template <typename ExplicitType, auto Accessor>
    struct nested_type {
      using type = ExplicitType;
    };

    template <auto Accessor>
    struct nested_type<void, Accessor> {
      using type = typename traits::nested_type<Accessor>::type;
    };

    template <typename ExplicitType, auto Accessor>
    using nested_type_t = typename nested_type<ExplicitType, Accessor>::type;
  } // namespace detail

  // -----------------------------------------------------------------------------
  // Lazy Wrapper Implementation
  // -----------------------------------------------------------------------------

  template <concepts::table T, typename Policy = history_mt_policy>
  class table {
    static_assert(concepts::history<Policy, detail::history<typename Policy::mutex_type>>);
  };

  template <concepts::table T, typename Policy>
    requires concepts::history<Policy, detail::history<typename Policy::mutex_type>>
  class table<T, Policy> {
    using context_t = detail::history<typename Policy::mutex_type>;
    using history_t = typename Policy::template storage_type<context_t>;

    template <auto Accessor, typename ExplicitType>
    using nested_t = detail::nested_type_t<ExplicitType, Accessor>;

    template <auto Accessor, typename ExplicitType>
    using nested_table_t = table<nested_t<Accessor, ExplicitType>, Policy>;

    template <concepts::table NestedT>
    static bool verify(uint8_t const* data, size_t size) {
      // clang-format off
      auto verifier = flatbuffers::Verifier{data, size,
        flatbuffers::Verifier::Options{.check_nested_flatbuffers = false}
      };
      // clang-format on

      return verifier.template VerifyBuffer<NestedT>(nullptr);
    }

  public:
    [[nodiscard]]
    static auto create(const uint8_t* data, size_t size) -> std::optional<table<T, Policy>> {
      if (!data || size == 0) [[unlikely]] {
        return std::nullopt;
      } else if (verify<T>(data, size)) {
        return table{flatbuffers::GetRoot<T>(data), Policy::template make_context<context_t>()};
      }

      return std::nullopt;
    }

    [[nodiscard]] auto operator->() const noexcept -> const T* {
      return table_;
    }

    [[nodiscard]] auto get() const noexcept -> const T* {
      return table_;
    }

    template <auto Accessor, typename ExplicitType = void>
    [[nodiscard]] auto get_nested() const -> std::optional<nested_table_t<Accessor, ExplicitType>> {
      using nested_type = nested_t<Accessor, ExplicitType>;
      using table_type  = nested_table_t<Accessor, ExplicitType>;

      const auto* vec = (table_->*Accessor)();

      if (not vec or not vec->size()) {
        return std::nullopt;
      }

      const auto initial_history_size = history_->cache.size(); // Capture initial size

      // Helper lambda to find the nested table in a specified range of history
      auto find_in_history_range = [&](size_t start_idx, size_t end_idx) -> std::optional<table_type> {
        for (size_t i = start_idx; i < end_idx; ++i) {
          const auto& [ptr, hist_id] = history_->cache[i];

          if (ptr == vec->data() && hist_id == detail::id<Accessor>) {
            return table_type(flatbuffers::GetRoot<nested_type>(vec->data()), history_);
          }
        }

        return std::nullopt;
      };

      // 1. Attempt to find the entry with a read lock first
      if (auto lock = typename Policy::read_lock(history_->mutex); true) {
        if (auto found_table = find_in_history_range(0, initial_history_size)) {
          return *found_table;
        }
      }

      // 2. If not found, acquire a write lock
      auto lock = typename Policy::write_lock(history_->mutex);

      // 3. Re-check history for elements potentially added by other threads
      //    while waiting for the write lock. This loop only checks new entries.
      if (auto found_table = find_in_history_range(initial_history_size, history_->cache.size())) {
        return *found_table;
      }

      // 4. If still not found, proceed to verify and add the new entry
      if (not verify<nested_type>(vec->data(), vec->size())) {
        return std::nullopt;
      } else {
        history_->cache.emplace_back(vec->data(), detail::id<Accessor>);
      }

      return table_type(flatbuffers::GetRoot<nested_type>(vec->data()), history_);
    }

  private:
    [[nodiscard]] table(const T* table, history_t history)
      : table_(table)
      , history_(std::move(history)) {}

    const T*  table_;
    history_t history_;

    template <concepts::table U, typename P>
    friend class table;
  };

} // namespace vault::fb
