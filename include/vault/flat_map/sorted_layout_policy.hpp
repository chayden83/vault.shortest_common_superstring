#ifndef SORTED_LAYOUT_POLICY_HPP
#define SORTED_LAYOUT_POLICY_HPP

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <vault/algorithm/amac.hpp>

#include "concepts.hpp"

namespace eytzinger {

  template <std::size_t Arity = 2>
  struct sorted_layout_policy {
    static constexpr inline auto const ARITY  = Arity;
    static constexpr inline auto const FANOUT = ARITY - 1;

    /**
     * @brief Unique identifier of verison 1 of the
     * sorted_layout_policy.
     */
    static constexpr inline auto const UID_V001 = 4185834535822629149uLL;

    template <typename I>
    struct is_compatible_key_iterator {
      static constexpr bool value = std::random_access_iterator<I>;
    };

    struct sorted_rank_to_index_fn {
      [[nodiscard]] static constexpr std::size_t operator()(std::size_t rank, std::size_t n) noexcept {
        assert(rank < n && "Rank out of bounds");
        return rank;
      }
    };

    struct index_to_sorted_rank_fn {
      [[nodiscard]] static constexpr std::size_t operator()(std::size_t i, std::size_t n) noexcept {
        assert(i < n && "Index out of bounds");
        return i;
      }
    };

    static constexpr inline sorted_rank_to_index_fn sorted_rank_to_index{};
    static constexpr inline index_to_sorted_rank_fn index_to_sorted_rank{};

    struct permute_fn {
      template <typename... Args>
      static constexpr void operator()(Args&&...) noexcept {}
    };

    struct get_nth_sorted_fn {
      template <std::random_access_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr std::iter_reference_t<I> operator()(I first, S last, std::size_t n) {
        const auto size = static_cast<std::size_t>(std::distance(first, last));
        if (n >= size) {
          throw std::out_of_range("sorted_layout_policy index out of range");
        }
        assert(n < size);
        return *(first + n);
      }

      template <std::ranges::random_access_range R>
      [[nodiscard]] static constexpr std::ranges::range_reference_t<R> operator()(R&& range, std::size_t n) {
        return operator()(std::ranges::begin(range), std::ranges::end(range), n);
      }
    };

    struct next_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t operator()(std::ptrdiff_t i, std::size_t n_sz) noexcept {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        assert(i >= 0 && i < n && "Cannot increment end iterator");
        return i + 1;
      }
    };

    struct prev_index_fn {
      [[nodiscard]] static constexpr std::ptrdiff_t operator()(std::ptrdiff_t i, std::size_t n_sz) noexcept {
        std::ptrdiff_t n = static_cast<std::ptrdiff_t>(n_sz);
        assert(i >= 0 && i <= n && "Index out of bounds");

        if (i == n) {
          return (n == 0) ? n : n - 1; // Decrementing end() -> last element
        } else if (i == 0) {
          return n; // Decrementing begin() -> underflow to sentinel
        } else {
          return i - 1;
        }
      }
    };

    struct lower_bound_fn {
      template <
        std::random_access_iterator I,
        std::sentinel_for<I>        S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr I operator()(I first, S last, const T& value, Comp comp = {}, Proj proj = {}) {
        return std::ranges::lower_bound(first, last, value, comp, proj);
      }

      template <
        std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R>
      operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {}) {
        return std::ranges::lower_bound(range, value, comp, proj);
      }
    };

    struct upper_bound_fn {
      template <
        std::random_access_iterator I,
        std::sentinel_for<I>        S,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr I operator()(I first, S last, const T& value, Comp comp = {}, Proj proj = {}) {
        return std::ranges::upper_bound(first, last, value, comp, proj);
      }

      template <
        std::ranges::random_access_range R,
        typename T,
        typename Comp = std::ranges::less,
        typename Proj = std::identity>
      [[nodiscard]] static constexpr std::ranges::iterator_t<R>
      operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {}) {
        return std::ranges::upper_bound(range, value, comp, proj);
      }
    };

    static constexpr inline permute_fn        permute{};
    static constexpr inline get_nth_sorted_fn get_nth_sorted{};
    static constexpr inline next_index_fn     next_index{};
    static constexpr inline prev_index_fn     prev_index{};
    static constexpr inline lower_bound_fn    lower_bound{};
    static constexpr inline upper_bound_fn    upper_bound{};

    // --- Batch Support ---

    template <typename HaystackIter, typename NeedleIter>
    struct search_state {
      NeedleIter   needle_cursor;
      HaystackIter haystack_cursor;
      HaystackIter haystack_last;

      [[nodiscard]] explicit search_state(NeedleIter n)
        : needle_cursor(n) {}

      [[nodiscard]] NeedleIter get_needle_cursor() const {
        return needle_cursor;
      }

      [[nodiscard]] HaystackIter result(HaystackIter /*begin*/) const {
        return haystack_cursor;
      }
    };

    template <typename HaystackIter, typename Comp, search_bound Bound>
    struct search_context {
      HaystackIter               begin_it;
      HaystackIter               end_it;
      [[no_unique_address]] Comp comp;

      [[nodiscard]] constexpr explicit search_context(HaystackIter first, HaystackIter last, Comp c)
        : begin_it(first)
        , end_it(last)
        , comp(c) {}

      [[nodiscard]] static constexpr uint64_t fanout() {
        return sorted_layout_policy::FANOUT;
      }

      template <std::forward_iterator I, std::sentinel_for<I> S>
      [[nodiscard]] static constexpr std::array<I, FANOUT> kary_pivots(I first, S last) {
        auto chunk_size = std::distance(first, last) / ARITY;

        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
          return std::array<I, FANOUT>{std::next(first, chunk_size * (Is + 1))...};
        }(std::make_index_sequence<FANOUT>{});
      }

      template <typename State>
      [[nodiscard]] constexpr vault::amac::step_result<FANOUT> init(State& state) const {
        using result = vault::amac::step_result<FANOUT>;

        // Initialize the range for the job
        state.haystack_cursor = begin_it;
        state.haystack_last   = end_it;

        if (state.haystack_cursor == state.haystack_last) {
          return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return result{((void)Is, nullptr)...};
          }(std::make_index_sequence<FANOUT>{});
        } else {
          auto pivots = kary_pivots(state.haystack_cursor, state.haystack_last);

          return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return result{std::addressof(*std::get<Is>(pivots))...};
          }(std::make_index_sequence<FANOUT>{});
        }
      }

      template <typename State>
      [[nodiscard]] constexpr vault::amac::step_result<FANOUT> step(State& state) const {
        using result = vault::amac::step_result<FANOUT>;

        auto pivots = kary_pivots(state.haystack_cursor, state.haystack_last);

        for (auto&& pivot : pivots) {
          assert(pivot != state.haystack_last && "Pivot should never be one past the end of the haystack");

          // Standard upper_bound logic: !comp(value, element) -> right
          // Standard lower_bound logic: comp(element, value) -> right
          bool go_right = [&] {
            if constexpr (Bound == search_bound::upper) {
              // pivot <= needle?
              return !std::invoke(comp, *state.needle_cursor, *pivot);
            } else {
              // pivot < needle?
              return std::invoke(comp, *pivot, *state.needle_cursor);
            }
          }();

          if (go_right) {
            state.haystack_cursor = std::next(pivot);
          } else {
            state.haystack_last = pivot;
            break;
          }
        }

        if (state.haystack_cursor == state.haystack_last) {
          return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return result{((void)Is, nullptr)...};
          }(std::make_index_sequence<FANOUT>{});
        } else {
          auto next_pivots = kary_pivots(state.haystack_cursor, state.haystack_last);
          return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return result{std::addressof(*std::get<Is>(next_pivots))...};
          }(std::make_index_sequence<FANOUT>{});
        }
      }
    };
  };

} // namespace eytzinger

#endif // SORTED_LAYOUT_POLICY_HPP
