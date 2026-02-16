/**
 * @file flatbuffers.hpp
 * @brief Policy-based lazy FlatBuffer wrapper with fixed concepts and traits.
 */
#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>
#include <boost/smart_ptr/make_local_shared_array.hpp>

namespace vault::fb::concepts {
  template <typename T>
  concept table = std::is_base_of_v<flatbuffers::Table, T>;

  template <template <typename> typename T>
  concept synchronized_history = requires(T<int>& history) {
    { history.with_read_lock(std::identity{}) } -> std::same_as<int const&>;
    { history.with_write_lock(std::identity{}) } -> std::same_as<int&>;
  };
} // namespace vault::fb::concepts

namespace vault::fb::traits {
  template <auto Accessor>
  struct nested_type;
}

namespace vault::fb {
  using accessor_id = std::array<std::byte, 16>;

  template <typename HistoryT>
  class synchronized_t {
    using history_t = HistoryT;

    struct impl_t {
      std::shared_mutex mutex;
      history_t         history;
    };

    std::shared_ptr<impl_t> impl = std::make_shared<impl_t>();

  public:
    template <std::invocable<history_t const&> ActionT>
    std::invoke_result_t<ActionT, history_t const&> with_read_lock(ActionT action) const {
      auto lock = std::shared_lock{impl->mutex};
      return std::invoke(action, impl->history);
    }

    template <std::invocable<history_t&> ActionT>
    std::invoke_result_t<ActionT, history_t&> with_write_lock(ActionT action) {
      auto lock = std::unique_lock{impl->mutex};
      return std::invoke(action, impl->history);
    }
  };

  template <typename HistoryT>
  class unsynchronized_t {
    boost::local_shared_ptr<HistoryT> impl = boost::make_local_shared<HistoryT>();

  public:
    template <std::invocable<HistoryT const&> ActionT>
    std::invoke_result_t<ActionT, HistoryT const&> with_read_lock(ActionT action) const {
      return std::invoke(action, *impl);
    }

    template <std::invocable<HistoryT&> ActionT>
    std::invoke_result_t<ActionT, HistoryT&> with_write_lock(ActionT action) {
      return std::invoke(action, *impl);
    }
  };

  // std::vector<std::pair<const uint8_t*, accessor_id>>

  namespace detail {
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

  template <typename T, template <typename> typename H = synchronized_t>
  class table {
    static_assert(concepts::table<T> && concepts::synchronized_history<H>);
  };

  template <typename T, template <typename> typename H>
    requires concepts::table<T> && concepts::synchronized_history<H>
  class table<T, H> {
    using history_t = H<std::vector<std::pair<const uint8_t*, accessor_id>>>;

    template <auto Accessor, typename ExplicitType>
    using nested_t = detail::nested_type_t<ExplicitType, Accessor>;

    template <auto Accessor, typename ExplicitType>
    using nested_table_t = table<nested_t<Accessor, ExplicitType>, H>;

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
    static auto create(const uint8_t* data, size_t size) -> std::optional<table<T, H>> {
      if (!data || size == 0) [[unlikely]] {
        return std::nullopt;
      } else if (verify<T>(data, size)) {
        return table{flatbuffers::GetRoot<T>(data), history_t{}};
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

      auto const needle = std::pair{vec->data(), detail::id<Accessor>};

      auto [hsize, already_verified] = history_.with_read_lock([&](auto const& history) {
        return std::pair{std::ranges::size(history), std::ranges::contains(history, needle)};
      });

      if (already_verified) {
        return table_type(flatbuffers::GetRoot<nested_type>(vec->data()), history_);
      } else if (not verify<nested_type>(vec->data(), vec->size())) {
        return std::nullopt;
      }

      history_.with_write_lock([&](auto& history) {
        if (not std::ranges::contains(std::views::drop(history, hsize), needle)) {
          history.emplace_back(needle);
        }
      });

      return table_type(flatbuffers::GetRoot<nested_type>(vec->data()), history_);
    }

    template <auto Accessor>
      requires concepts::table<std::remove_cvref_t<decltype(*(std::declval<T*>()->*Accessor)()->Get(0))>>
    [[nodiscard]] auto get_list() const {
      auto const* fb_vector = (table_->*Accessor)();

      // clang-format off
      auto nth_table = [&, fb_vector](std::size_t n) {
	return vault::fb::table { fb_vector->Get(n), history_ };
      };

      return std::views::iota(0U, fb_vector ? fb_vector->size() : 0U)
	| std::views::transform(nth_table);
      // clang-format on
    }

  private:
    [[nodiscard]] table(const T* table, history_t history)
      : table_(table)
      , history_(std::move(history)) {

      assert(table != nullptr && "Initializing table with nullptr is invalid");
    }

    const T*          table_;
    mutable history_t history_;

    template <typename, template <typename> typename>
    friend class table;
  };

  template <typename T, template <typename> typename H, typename V>
  table(T*, H<V>) -> table<T, H>;

} // namespace vault::fb
