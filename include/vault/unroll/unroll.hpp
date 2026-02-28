#ifndef VAULT_UNROLL_HPP
#define VAULT_UNROLL_HPP

#include <bit>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <utility>

namespace vault::concepts {

  /**
   * Specifies the requirements for a displacement vector in an affine space.
   * * To interface correctly with compile-time loop unrolling factors, the
   * displacement type must be constructible from `std::size_t`. It must also
   * support the comparison and mutation operations required to calculate
   * remaining loop bounds.
   *
   * ### Template Parameters
   * - **D**: The type to check against the affine displacement constraints.
   */
  template <typename D>
  concept affine_displacement = std::constructible_from<D, std::size_t> && requires(D d) {
    { d >= d } -> std::same_as<bool>;
    { d -= d } -> std::same_as<D&>;
  };

  /**
   * Specifies that a type structurally models a coordinate in an affine space.
   *
   * Types satisfying this concept support affine space operations: subtracting
   * two coordinates yields a displacement (distance), and adding or subtracting
   * a displacement to/from a coordinate yields a new coordinate. The type must
   * also be totally ordered to facilitate loop bounds checking.
   *
   * ### Template Parameters
   * - **T**: The type to check against the affine coordinate constraints.
   */
  template <typename T>
  concept affine_coordinate = std::regular<T> && std::totally_ordered<T> && requires(T p) {
    // Subtracting two points yields a displacement
    { p - p };
  } && requires(T p, decltype(p - p) d) {
    // Affine translations
    { p + d } -> std::same_as<T>;
    { d + p } -> std::same_as<T>;
    { p - d } -> std::same_as<T>;

    // Mutation operations
    { p += d } -> std::same_as<T&>;
    { p -= d } -> std::same_as<T&>;
  } && affine_displacement<decltype(std::declval<T>() - std::declval<T>())>;

} // namespace vault::concepts

namespace vault::detail {

  /**
   * Executes a perfectly unrolled sequence of function calls.
   *
   * Utilizes a C++20 fold expression over a compile-time index sequence to
   * generate a strictly unrolled block of `N` invocations.
   *
   * ### Template Parameters
   * - **N**: The exact number of times to unroll the loop body.
   * - **Coordinate**: The affine coordinate type of the induction variable.
   * - **Func**: The callable type representing the loop body.
   *
   * ### Parameters
   * - **current**: The current induction variable (passed by reference, advanced by `N`).
   * - **func**: The callable to invoke with the current coordinate.
   */
  template <std::size_t N, vault::concepts::affine_coordinate Coordinate, typename Func>
  constexpr void unroll_exact(Coordinate& current, Func&& func) {
    using distance_t = decltype(current - current);

    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (..., (func(current + static_cast<distance_t>(Is))));
    }(std::make_index_sequence<N>{});

    current += static_cast<distance_t>(N);
  }

  /**
   * Recursively generates logarithmic tail unrolling blocks.
   *
   * Instantiates conditional unrolling blocks for sizes `Step`, `Step/2`,
   * `Step/4`, down to `1`. This strictly minimizes generated code size while
   * perfectly executing the remainder of the iteration space.
   *
   * ### Template Parameters
   * - **Step**: The current unrolling factor power of 2 to check.
   * - **Coordinate**: The affine coordinate type of the induction variable.
   * - **Distance**: The type of the displacement between coordinates.
   * - **Func**: The callable type representing the loop body.
   *
   * ### Parameters
   * - **current**: The current coordinate.
   * - **dist**: The remaining distance to execute.
   * - **func**: The callable to invoke.
   */
  template <std::size_t Step, vault::concepts::affine_coordinate Coordinate, typename Distance, typename Func>
  constexpr void unroll_tail(Coordinate& current, Distance& dist, Func&& func) {
    if constexpr (Step > 0) {
      auto step_dist = static_cast<Distance>(Step);
      if (dist >= step_dist) {
        unroll_exact<Step>(current, func);
        dist -= step_dist;
      }
      unroll_tail<Step / 2>(current, dist, std::forward<Func>(func));
    }
  }

} // namespace vault::detail

namespace vault {

  /**
   * Executes a generic, unrolled loop over a defined affine coordinate range.
   *
   * This function template unrolls a loop body by a compile-time factor `K`.
   * It handles the main loop body in blocks of `K`, and executes the remaining
   * iterations using a logarithmic tail execution strategy to minimize instruction
   * cache bloat.
   *
   * ### Template Parameters
   * - **K**: The unroll factor. Must be a power of 2.
   * - **Coordinate**: The type of the lower and upper bounds, modeling an affine coordinate.
   * - **Func**: The callable type representing the loop body.
   *
   * ### Parameters
   * - **lb**: The lower bound of the loop (inclusive).
   * - **ub**: The upper bound of the loop (exclusive).
   * - **func**: The callable to invoke for each iteration. It must accept a single
   * argument of type `Coordinate` by value.
   */
  template <std::size_t K, concepts::affine_coordinate Coordinate, typename Func>
  constexpr void unroll_loop(Coordinate lb, Coordinate ub, Func&& func) {
    static_assert(std::has_single_bit(K), "The unroll factor K must be a power of 2.");
    assert(lb <= ub && "The lower bound must not exceed the upper bound.");

    using distance_t = decltype(ub - lb);
    auto dist        = distance_t{ub - lb};
    auto current     = Coordinate{lb};
    auto k_dist      = static_cast<distance_t>(K);

    while (dist >= k_dist) {
      detail::unroll_exact<K>(current, func);
      dist -= k_dist;
    }

    detail::unroll_tail<K / 2>(current, dist, std::forward<Func>(func));
  }

} // namespace vault

#endif // VAULT_UNROLL_HPP
