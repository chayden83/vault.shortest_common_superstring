#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <numeric>
#include <vector>

#include <vault/unroll/unroll.hpp>

namespace {

  // ============================================================================
  // Custom Affine Coordinate Definitions
  // ============================================================================

  struct custom_displacement {
    int value{0};

    constexpr custom_displacement() = default;

    constexpr explicit custom_displacement(std::size_t v)
      : value{static_cast<int>(v)} {}

    constexpr explicit custom_displacement(int v)
      : value{v} {}

    [[nodiscard]] constexpr auto operator>=(const custom_displacement& rhs) const -> bool {
      return value >= rhs.value;
    }

    constexpr auto operator-=(const custom_displacement& rhs) -> custom_displacement& {
      value -= rhs.value;
      return *this;
    }
  };

  struct custom_coordinate {
    int value{0};

    constexpr auto operator<=>(const custom_coordinate&) const = default;

    [[nodiscard]] constexpr auto operator-(const custom_coordinate& rhs) const -> custom_displacement {
      return custom_displacement{value - rhs.value};
    }

    [[nodiscard]] constexpr auto operator+(const custom_displacement& d) const -> custom_coordinate {
      return custom_coordinate{value + d.value};
    }

    [[nodiscard]] friend constexpr auto operator+(const custom_displacement& d, const custom_coordinate& c)
      -> custom_coordinate {
      return custom_coordinate{c.value + d.value};
    }

    [[nodiscard]] constexpr auto operator-(const custom_displacement& d) const -> custom_coordinate {
      return custom_coordinate{value - d.value};
    }

    constexpr auto operator+=(const custom_displacement& d) -> custom_coordinate& {
      value += d.value;
      return *this;
    }

    constexpr auto operator-=(const custom_displacement& d) -> custom_coordinate& {
      value -= d.value;
      return *this;
    }
  };

  // Compile-time concept verification
  static_assert(vault::concepts::affine_coordinate<std::size_t>);
  static_assert(vault::concepts::affine_coordinate<std::vector<int>::iterator>);
  static_assert(vault::concepts::affine_coordinate<custom_coordinate>);

  // ============================================================================
  // Constexpr Execution Helpers
  // ============================================================================

  [[nodiscard]] constexpr auto test_integral_unroll_compile_time() -> int {
    auto sum = int{0};
    auto lb  = int{0};
    auto ub  = int{15}; // 15 iterations: 1x8, 1x4, 1x2, 1x1

    vault::unroll_loop<8>(lb, ub, [&](auto i) { sum += i; });

    return sum;
  }

  static_assert(test_integral_unroll_compile_time() == 105);

} // namespace

// ============================================================================
// Catch2 Test Cases
// ============================================================================

TEST_CASE("unroll_loop successfully processes standard integral bounds", "[unroll][integral]") {
  auto sum          = int{0};
  auto expected_sum = int{0};
  auto lb           = int{5};
  auto ub           = int{26}; // 21 iterations total. k=4: 5x4 main loop, 1x1 tail loop.

  for (auto i = lb; i < ub; ++i) {
    expected_sum += i;
  }

  vault::unroll_loop<4>(lb, ub, [&](auto i) { sum += i; });

  REQUIRE(sum == expected_sum);
}

TEST_CASE("unroll_loop successfully processes random access iterators", "[unroll][iterator]") {
  auto data          = std::vector<int>(23, 1); // 23 elements. k=8: 2x8 main loop, 1x4, 1x2, 1x1 tail loop.
  auto expected_data = std::vector<int>(23, 2);

  vault::unroll_loop<8>(data.begin(), data.end(), [](auto it) { *it *= 2; });

  REQUIRE(data == expected_data);
}

TEST_CASE("unroll_loop successfully processes custom affine coordinates", "[unroll][custom_coordinate]") {
  auto iterations = int{0};
  auto lb         = custom_coordinate{10};
  auto ub         = custom_coordinate{42}; // 32 iterations total. k=16: 2x16 main loop, 0 tail loop.

  vault::unroll_loop<16>(lb, ub, [&](auto coord) {
    iterations++;
    // Verify the coordinate value is correct for this iteration
    REQUIRE(coord.value == 10 + iterations - 1);
  });

  REQUIRE(iterations == 32);
}

TEST_CASE("unroll_loop processes empty ranges correctly without executing", "[unroll][edge_case]") {
  auto executions = int{0};
  auto lb         = std::size_t{10};
  auto ub         = std::size_t{10};

  vault::unroll_loop<8>(lb, ub, [&](auto) { executions++; });

  REQUIRE(executions == 0);
}
