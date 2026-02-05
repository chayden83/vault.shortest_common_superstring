#ifndef SORTED_LAYOUT_POLICY_HPP
#define SORTED_LAYOUT_POLICY_HPP

// ... (includes unchanged) ...
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <vault/algorithm/amac.hpp>

namespace eytzinger {

  template <std::size_t Arity = 2> struct sorted_layout_policy {
    static constexpr inline auto const ARITY    = Arity;
    static constexpr inline auto const FANOUT   = ARITY - 1;
    static constexpr inline auto const UID_V001 = 4185834535822629149uLL;

    // ... (Helper functors unchanged) ...
    template <typename I> struct is_compatible_key_iterator {
      static constexpr bool value = std::random_access_iterator<I>;
    };

    struct sorted_rank_to_index_fn {
      [[nodiscard]] constexpr std::size_t operator()(
        std::size_t rank, std::size_t n) const noexcept
      {
        assert(rank < n);
        return rank;
      }
    };

    struct index_to_sorted_rank_fn {
      [[nodiscard]] constexpr std::size_t operator()(
        std::size_t i, std::size_t n) const noexcept
      {
        assert(i < n);
        return i;
      }
    };

    static constexpr inline sorted_rank_to_index_fn sorted_rank_to_index{};
    static constexpr inline index_to_sorted_rank_fn index_to_sorted_rank{};

    struct permute_fn {
      template <typename... Args>
      constexpr void operator()(Args&&...) const noexcept
      {}
    };

    static constexpr inline permute_fn permute{};

    // ... (next_index, prev_index, get_nth_sorted, lower_bound, upper_bound
    // functors omitted for brevity but present) ...
    struct get_nth_sorted_fn {
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] constexpr std::iter_reference_t<I> operator()(
        I first, S last, std::size_t n) const
      {
        const auto size = static_cast<std::size_t>(std::distance(first, last));
        if (n >= size) {
          throw std::out_of_range("sorted");
        }
        return *(first + n);
      }

      template <std::ranges::random_access_range R>
      [[nodiscard]] constexpr std::ranges::range_reference_t<R> operator()(
        R&& range, std::size_t n) const
      {
        return operator()(
          std::ranges::begin(range), std::ranges::end(range), n);
      }
    };

    struct next_index_fn {
      [[nodiscard]] constexpr std::ptrdiff_t operator()(
        std::ptrdiff_t i, std::size_t n_sz) const noexcept
      {
        return i + 1;
      }
    };

    struct prev_index_fn {
      [[nodiscard]] constexpr std::ptrdiff_t operator()(
        std::ptrdiff_t i, std::size_t n_sz) const noexcept
      {
        return (i == static_cast<std::ptrdiff_t>(n_sz))
          ? (n_sz == 0 ? 0 : n_sz - 1)
          : (i - 1);
      }
    };

    struct lower_bound_fn {
      template <std::random_access_iterator I,
        std::sentinel_for<I>                S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr I operator()(
        I first, S last, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        return std::ranges::lower_bound(first, last, value, comp, proj);
      }

      template <std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr std::ranges::iterator_t<R> operator()(
        R&& range, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        return std::ranges::lower_bound(range, value, comp, proj);
      }
    };

    struct upper_bound_fn {
      template <std::random_access_iterator I,
        std::sentinel_for<I>                S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr I operator()(
        I first, S last, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        return std::ranges::upper_bound(first, last, value, comp, proj);
      }

      template <std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] constexpr std::ranges::iterator_t<R> operator()(
        R&& range, const T& value, Comp comp = {}, Proj proj = {}) const
      {
        return std::ranges::upper_bound(range, value, comp, proj);
      }
    };

    static constexpr inline get_nth_sorted_fn get_nth_sorted{};
    static constexpr inline next_index_fn     next_index{};
    static constexpr inline prev_index_fn     prev_index{};
    static constexpr inline lower_bound_fn    lower_bound{};
    static constexpr inline upper_bound_fn    upper_bound{};

    // --- AMAC Implementation ---

    template <typename HaystackI, typename NeedleI> struct kary_search_state {
      NeedleI needle_iter; // Renamed from needle_cursor to match layout_map
                           // expectation
      HaystackI haystack_cursor;
      HaystackI haystack_last;
    };

    template <typename Compare> struct kary_search_context {
      [[no_unique_address]] Compare compare_;

      static constexpr uint64_t fanout() { return FANOUT; }

      // Helper to access result after completion
      template <typename State>
      [[nodiscard]] auto get_result(State const& s) const
      {
        return s.haystack_cursor;
      }

      template <std::forward_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr std::array<I, FANOUT> kary_pivots(
        I first, S last)
      {
        auto chunk_size = std::distance(first, last) / ARITY;
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
          return std::array<I, FANOUT>{
            std::next(first, chunk_size * (Is + 1))...};
        }(std::make_index_sequence<FANOUT>{});
      }

      template <typename Job, typename Emit>
      [[nodiscard]] constexpr vault::amac::step_result<FANOUT> init(
        Job& state, Emit&&) const
      {
        using result_t = vault::amac::step_result<FANOUT>;
        if (state.haystack_cursor == state.haystack_last) {
          return result_t{};
        }
        auto pivots = kary_pivots(state.haystack_cursor, state.haystack_last);
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
          return result_t{std::addressof(*std::get<Is>(pivots))...};
        }(std::make_index_sequence<FANOUT>{});
      }

      template <typename Job, typename Emit>
      [[nodiscard]] constexpr vault::amac::step_result<FANOUT> step(
        Job& state, Emit&& emit) const
      {
        auto pivots = kary_pivots(state.haystack_cursor, state.haystack_last);
        for (auto&& pivot : pivots) {
          if (std::invoke(compare_, *pivot, *state.needle_iter)) {
            state.haystack_cursor = std::next(pivot);
          } else {
            state.haystack_last = pivot;
            break;
          }
        }
        return init(state, emit);
      }
    };

    struct search_state_fn {
      template <std::ranges::forward_range Haystack,
        std::forward_iterator              NeedleI>
      [[nodiscard]] constexpr auto operator()(
        Haystack const& haystack, NeedleI needle) const
      {
        using HaystackI =
          std::ranges::iterator_t<std::remove_reference_t<Haystack const>>;
        return kary_search_state<HaystackI, NeedleI>{
          needle, std::ranges::begin(haystack), std::ranges::end(haystack)};
      }
    };

    struct lower_bound_context_fn {
      template <typename Compare = std::ranges::less>
      [[nodiscard]] constexpr auto operator()(Compare compare = {}) const
      {
        return kary_search_context<Compare>{compare};
      }
    };

    struct upper_bound_context_fn {
      template <typename Compare = std::ranges::less>
      [[nodiscard]] constexpr auto operator()(Compare compare = {}) const
      {
        auto adapted = [=](auto const& a, auto const& b) {
          return !std::invoke(compare, b, a);
        };
        return kary_search_context<decltype(adapted)>{adapted};
      }
    };

    static constexpr inline search_state_fn        make_state{};
    static constexpr inline lower_bound_context_fn lower_bound_context{};
    static constexpr inline upper_bound_context_fn upper_bound_context{};
  };

} // namespace eytzinger

#endif
