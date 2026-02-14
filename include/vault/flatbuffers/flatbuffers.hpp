/**
 * @file flatbuffers.hpp
 * @brief Policy-based lazy FlatBuffer wrapper with fixed concepts and traits.
 */
#pragma once

#include <concepts>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>

#include "flatbuffers/verifier.h"

namespace vault::fb {
  namespace concepts {
    template <typename T>
    concept table = std::is_base_of_v<flatbuffers::Table, T>;

    template <typename P, typename T>
    concept table_cache = requires {
      typename P::mutex_type;
      typename P::read_lock;
      typename P::write_lock;

      { P::template make_context<T>() } -> std::same_as<typename P::template storage_type<T>>;
    };
  } // namespace concepts

  namespace traits {
    template <auto Accessor>
    struct nested_type;
  }

  using accessor_id = std::array<std::byte, 16>;

  struct table_cache_mt {
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

  struct table_cache_st {
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
    struct verification_context {
      using key_type = std::pair<const uint8_t*, accessor_id>;
      std::vector<key_type> history;
      mutable MutexType     mutex;
    };

    template <auto Accessor>
    const accessor_id id = []() {
      accessor_id id{};
      auto        accessor_val = Accessor;
      std::memcpy(id.data(), &accessor_val, sizeof(accessor_val));
      return id;
    }();

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

  template <concepts::table T, typename Policy = table_cache_mt>
  class table {
    static_assert(concepts::table_cache<Policy, detail::verification_context<typename Policy::mutex_type>>);
  };

  template <concepts::table T, typename Policy>
    requires concepts::table_cache<Policy, detail::verification_context<typename Policy::mutex_type>>
  class table<T, Policy> {
    using context_t = detail::verification_context<typename Policy::mutex_type>;
    using storage_t = typename Policy::template storage_type<context_t>;

    template <auto Accessor, typename ExplicitType>
    using nested_t = detail::nested_type_t<ExplicitType, Accessor>;

    template <auto Accessor, typename ExplicitType>
    using nested_table_t = table<nested_t<Accessor, ExplicitType>, Policy>;

  public:
    [[nodiscard]]
    static auto create(const uint8_t* data, size_t size) -> std::optional<table<T, Policy>> {
      if (!data || size == 0) [[unlikely]] {
        return std::nullopt;
      }

      // clang-format off
      auto verifier = flatbuffers::Verifier {
	data, size, flatbuffers::Verifier::Options {.check_nested_flatbuffers = false}
      };
      // clang-format on

      if (verifier.template VerifyBuffer<T>(nullptr)) {
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

      if (!vec) {
        return std::nullopt;
      }

      const auto* nested_data  = vec->data();
      const auto  history_size = ctx_->history.size();

      {
        auto lock = typename Policy::read_lock(ctx_->mutex);

        for (const auto& [ptr, hist_id] : ctx_->history) {
          if (ptr == nested_data && hist_id == detail::id<Accessor>) {
            return table_type(flatbuffers::GetRoot<nested_type>(nested_data), ctx_);
          }
        }
      }

      auto lock = typename Policy::write_lock(ctx_->mutex);

      for (auto i = history_size; i != ctx_->history.size(); ++i) {
        const auto& [ptr, hist_id] = ctx_->history[i];

        if (ptr == nested_data && hist_id == detail::id<Accessor>) {
          return table_type(flatbuffers::GetRoot<nested_type>(nested_data), ctx_);
        }
      }

      auto verifier = flatbuffers::Verifier{nested_data, vec->size()};

      if (not verifier.template VerifyBuffer<nested_type>(nullptr)) {
        return std::nullopt;
      } else {
        ctx_->history.emplace_back(nested_data, detail::id<Accessor>);
      }

      return table_type(flatbuffers::GetRoot<nested_type>(nested_data), ctx_);
    }

  private:
    [[nodiscard]] table(const T* table, storage_t ctx) : table_(table), ctx_(std::move(ctx)) {}

    const T*  table_;
    storage_t ctx_;

    template <concepts::table U, typename P>
    friend class table;
  };

} // namespace vault::fb
