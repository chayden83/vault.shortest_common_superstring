/**
 * @file lazy_wrapper.hpp
 * @brief Generic, policy-based, thread-safe lazy FlatBuffer wrapper.
 * Optimized with Non-Type Template Parameters (NTTP).
 */
#pragma once

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <vault/frozen_vector/local_shared_ptr.hpp>

namespace lazyfb {

  /// @brief Concepts used to constrain template arguments for the lazy wrapper.
  namespace concepts {

    template <typename T>
    concept flatbuffer_table = std::is_base_of_v<flatbuffers::Table, T>;

  } // namespace concepts

  /// @brief Type-erased identifier for member function pointers (16 bytes).
  using accessor_id = std::array<std::byte, 16>;

  /// @brief Concurrency and storage policies.
  namespace policies {

    struct thread_safe {
      using mutex_type = std::shared_mutex;

      template <typename T> using storage_type = std::shared_ptr<T>;

      using read_lock  = std::shared_lock<mutex_type>;
      using write_lock = std::unique_lock<mutex_type>;

      template <typename Context, typename... Args>
      static auto make_context(Args&&... args) -> storage_type<Context>
      {
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

      template <typename T> using storage_type = frozen::local_shared_ptr<T>;

      struct read_lock {
        explicit constexpr read_lock(mutex_type&) noexcept {}
      };

      struct write_lock {
        explicit constexpr write_lock(mutex_type&) noexcept {}
      };

      template <typename Context, typename... Args>
      static auto make_context(Args&&... args) -> storage_type<Context>
      {
        return frozen::local_shared_ptr<Context>(
          new Context{std::forward<Args>(args)...});
      }
    };

  } // namespace policies

  // -----------------------------------------------------------------------------
  // Internal Structures
  // -----------------------------------------------------------------------------

  namespace detail {

    template <typename MutexType> struct verification_context {
      using key_type = std::pair<const uint8_t*, accessor_id>;
      std::vector<key_type> history;
      mutable MutexType     mutex;
    };

    /**
     * @brief Helper to statically generate IDs for template accessors.
     */
    template <auto Accessor> struct id_generator {
      static const accessor_id value;
    };

    template <auto Accessor>
    const accessor_id id_generator<Accessor>::value = []() {
      accessor_id id{};
      // FIX: Assign NTTP to a local variable to be able to take its address.
      auto accessor_value = Accessor;

      // We can safely memcpy here because this lambda runs only once at
      // initialization. The 'Accessor' value is known at compile/link time.
      std::memcpy(id.data(), &accessor_value, sizeof(accessor_value));
      return id;
    }();

  } // namespace detail

  // -----------------------------------------------------------------------------
  // Lazy Wrapper
  // -----------------------------------------------------------------------------

  template <concepts::flatbuffer_table T,
    typename Policy = policies::thread_safe>
  class lazy_wrapper {
  public:
    using context_type =
      detail::verification_context<typename Policy::mutex_type>;

    /**
     * @brief Creates a lazy wrapper for a root table.
     */
    [[nodiscard]]
    static auto create(const uint8_t* data, size_t size)
      -> std::optional<lazy_wrapper<T, Policy>>
    {
      if (!data || size == 0) [[unlikely]] {
        return std::nullopt;
      }

      auto options                     = flatbuffers::Verifier::Options{};
      options.check_nested_flatbuffers = false;

      auto verifier = flatbuffers::Verifier{data, size, options};

      if (verifier.template VerifyBuffer<T>(nullptr)) {
        return lazy_wrapper{flatbuffers::GetRoot<T>(data),
          Policy::template make_context<context_type>()};
      }
      return std::nullopt;
    }

    [[nodiscard]] auto operator->() const noexcept -> const T*
    {
      return table_;
    }

    [[nodiscard]] auto get() const noexcept -> const T* { return table_; }

    /**
     * @brief Lazily verifies a nested FlatBuffer using a compile-time accessor.
     * * @tparam Accessor The member function pointer (e.g.,
     * &Monster::equipped_gear).
     * @tparam NestedT The expected type of the nested table.
     */
    template <auto Accessor, concepts::flatbuffer_table NestedT>
    [[nodiscard]]
    auto get_nested() const -> std::optional<lazy_wrapper<NestedT, Policy>>
    {
      // Compile-time check to ensure Accessor is actually a member of T
      // and returns a Vector<uint8_t>*.
      static_assert(std::is_same_v<decltype((std::declval<T>().*Accessor)()),
                      const flatbuffers::Vector<uint8_t>*>,
        "Accessor must be a member function returning const "
        "flatbuffers::Vector<uint8_t>*");

      const auto* vec = (table_->*Accessor)();

      // Branch prediction hint: valid data usually has fields present
      if (!vec) [[unlikely]] {
        return std::nullopt;
      }

      const auto* nested_data = vec->data();

      // STATIC OPTIMIZATION:
      // This ID is generated once per program lifetime for this specific
      // Accessor. No runtime memcpy or overhead.
      const auto& id = detail::id_generator<Accessor>::value;

      // Phase 1: Read Lock (Fast Path)
      {
        auto lock = typename Policy::read_lock(ctx_->mutex);
        for (const auto& [ptr, hist_id] : ctx_->history) {
          if (ptr == nested_data && hist_id == id) {
            return lazy_wrapper<NestedT, Policy>(
              flatbuffers::GetRoot<NestedT>(nested_data), ctx_);
          }
        }
      }

      // Phase 2: Write Lock (Slow Path)
      auto lock = typename Policy::write_lock(ctx_->mutex);

      for (const auto& [ptr, hist_id] : ctx_->history) {
        if (ptr == nested_data && hist_id == id) {
          return lazy_wrapper<NestedT, Policy>(
            flatbuffers::GetRoot<NestedT>(nested_data), ctx_);
        }
      }

      // Verify
      auto options                     = flatbuffers::Verifier::Options{};
      options.check_nested_flatbuffers = false;
      auto verifier = flatbuffers::Verifier{nested_data, vec->size(), options};

      if (verifier.template VerifyBuffer<NestedT>(nullptr)) {
        ctx_->history.emplace_back(nested_data, id);
        return lazy_wrapper<NestedT, Policy>(
          flatbuffers::GetRoot<NestedT>(nested_data), ctx_);
      }

      return std::nullopt;
    }

  private:
    explicit lazy_wrapper(
      const T* table, typename Policy::template storage_type<context_type> ctx)
        : table_(table)
        , ctx_(std::move(ctx))
    {}

    const T*                                             table_;
    typename Policy::template storage_type<context_type> ctx_;

    template <concepts::flatbuffer_table U, typename P>
    friend class lazy_wrapper;
  };

} // namespace lazyfb
