/**
 * @file flatbuffers.hpp
 * @brief Policy-based lazy FlatBuffer wrapper with fixed concepts and traits.
 */
#pragma once

#include <concepts>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>

namespace lazyfb {

  namespace concepts {

    template <typename T>
    concept flatbuffer_table = std::is_base_of_v<flatbuffers::Table, T>;

    template <typename P, typename T>
    concept cache_policy = requires {
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

  namespace policies {

    struct thread_safe {
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

    struct single_threaded {
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

  } // namespace policies

  namespace detail {
    template <typename MutexType>
    struct verification_context {
      using key_type = std::pair<const uint8_t*, accessor_id>;
      std::vector<key_type> history;
      mutable MutexType     mutex;
    };

    template <auto Accessor>
    struct id_generator {
      static const accessor_id value;
    };

    template <auto Accessor>
    const accessor_id id_generator<Accessor>::value = []() {
      accessor_id id{};
      auto        accessor_val = Accessor;
      std::memcpy(id.data(), &accessor_val, sizeof(accessor_val));
      return id;
    }();

    template <typename ExplicitType, auto Accessor>
    struct resolve_nested_type {
      using type = ExplicitType;
    };

    template <auto Accessor>
    struct resolve_nested_type<void, Accessor> {
      using type = typename traits::nested_type<Accessor>::type;
    };
  } // namespace detail

  // -----------------------------------------------------------------------------
  // Lazy Wrapper Implementation
  // -----------------------------------------------------------------------------

  template <concepts::flatbuffer_table T, typename Policy = policies::thread_safe>
  class lazy_wrapper {
    static_assert(concepts::cache_policy<Policy, detail::verification_context<typename Policy::mutex_type>>);
  };

  template <concepts::flatbuffer_table T, typename Policy>
    requires concepts::cache_policy<Policy, detail::verification_context<typename Policy::mutex_type>>
  class lazy_wrapper<T, Policy> {
  public:
    using context_type = detail::verification_context<typename Policy::mutex_type>;

    [[nodiscard]]
    static auto create(const uint8_t* data, size_t size) -> std::optional<lazy_wrapper<T, Policy>> {
      if (!data || size == 0) [[unlikely]]
        return std::nullopt;

      auto options                     = flatbuffers::Verifier::Options{};
      options.check_nested_flatbuffers = false;
      auto verifier                    = flatbuffers::Verifier{data, size, options};

      if (verifier.template VerifyBuffer<T>(nullptr)) {
        return lazy_wrapper{flatbuffers::GetRoot<T>(data), Policy::template make_context<context_type>()};
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
    [[nodiscard]] auto get_nested() const {
      using NestedT = typename detail::resolve_nested_type<ExplicitType, Accessor>::type;
      static_assert(concepts::flatbuffer_table<NestedT>);

      using return_type = std::optional<lazy_wrapper<NestedT, Policy>>;
      const auto* vec   = (table_->*Accessor)();
      if (!vec)
        return return_type{std::nullopt};

      const auto* nested_data = vec->data();
      const auto& id          = detail::id_generator<Accessor>::value;

      {
        auto lock = typename Policy::read_lock(ctx_->mutex);
        for (const auto& [ptr, hist_id] : ctx_->history) {
          if (ptr == nested_data && hist_id == id) {
            return return_type(lazy_wrapper<NestedT, Policy>(flatbuffers::GetRoot<NestedT>(nested_data), ctx_));
          }
        }
      }

      auto lock = typename Policy::write_lock(ctx_->mutex);
      for (const auto& [ptr, hist_id] : ctx_->history) {
        if (ptr == nested_data && hist_id == id) {
          return return_type(lazy_wrapper<NestedT, Policy>(flatbuffers::GetRoot<NestedT>(nested_data), ctx_));
        }
      }

      auto verifier = flatbuffers::Verifier{nested_data, vec->size()};
      if (verifier.template VerifyBuffer<NestedT>(nullptr)) {
        ctx_->history.emplace_back(nested_data, id);
        return return_type(lazy_wrapper<NestedT, Policy>(flatbuffers::GetRoot<NestedT>(nested_data), ctx_));
      }

      return return_type{std::nullopt};
    }

  private:
    using context_t = typename Policy::template storage_type<context_type>;

    explicit lazy_wrapper(const T* table, context_t ctx) : table_(table), ctx_(std::move(ctx)) {}

    const T*  table_;
    context_t ctx_;

    template <concepts::flatbuffer_table U, typename P>
    friend class lazy_wrapper;
  };

} // namespace lazyfb
