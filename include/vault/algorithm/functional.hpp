// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_FUNCTIONAL_HPP
#define VAULT_FUNCTIONAL_HPP

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

// clang-format off

namespace vlt::functional {
  template<typename T>
  struct construct_fn {
    template<typename... Args>
      requires std::constructible_from<T, Args&&...>
    [[nodiscard]] static constexpr T operator ()(Args&&... args) noexcept
      (std::is_nothrow_constructible_v<T, Args&&...>)
    {
      return T { std::forward<Args>(args)... };
    }
  };

  template<typename T>
  constexpr inline auto const construct = construct_fn<T> { };

  constexpr inline struct addressof_fn {
    template<typename T>
    [[nodiscard]] static constexpr T *operator ()(T &arg) noexcept {
      return std::addressof(arg);
    }
  } const addressof { };
}

// clang-format on

#endif
