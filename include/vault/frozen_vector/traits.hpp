#ifndef FROZEN_TRAITS_HPP
#define FROZEN_TRAITS_HPP

#include <cassert>
#include <concepts>
#include <memory>
#include <type_traits>

namespace frozen {

  // ============================================================================
  // POINTER TRAITS EXTENSION
  // ============================================================================

  template <typename T>
  concept HasUseCount = requires(T t) {
    { t.use_count() } -> std::convertible_to<long>;
  };

  template <typename Ptr> struct pointer_traits : std::pointer_traits<Ptr> {
    static constexpr bool is_reference_counted = HasUseCount<Ptr>;
  };

  // ============================================================================
  // FREEZE TRAITS
  // ============================================================================

  template <typename Src, typename Dest> struct freeze_traits {
    [[nodiscard]]
    static Dest freeze(Src&& src)
    {
      // Compile-Time Invariants
      static_assert(!std::is_const_v<Src>, "Source handle cannot be const");
      static_assert(
          !std::is_reference_v<Src>, "Source handle cannot be a reference"
      );

      // Priority 1: Direct Move Construction
      if constexpr (std::is_constructible_v<Dest, Src&&>) {
        return Dest(std::move(src));
      }
      // Priority 2: Manual Transfer via Raw Pointer
      else {
        auto* raw = src.release();

        // Runtime Invariant: If we released a pointer, we must be able to
        // construct Dest from it Note: raw might be nullptr, which is valid for
        // smart pointers

        return Dest(raw);
      }
    }
  };

} // namespace frozen

#endif // FROZEN_TRAITS_HPP
