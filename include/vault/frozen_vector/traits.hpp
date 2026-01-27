#ifndef FROZEN_TRAITS_HPP
#define FROZEN_TRAITS_HPP

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
  // FREEZE TRAITS (Default Implementation)
  // ============================================================================

  template <typename Src, typename Dest> struct freeze_traits {
    [[nodiscard]]
    static Dest freeze(Src&& src)
    {
      // Priority 1: Direct Move Construction (Safe)
      if constexpr (std::is_constructible_v<Dest, Src&&>) {
        return Dest(std::move(src));
      }
      // Priority 2: Manual Transfer via Raw Pointer
      // Fix: Must release ownership from src BEFORE creating Dest to prevent
      // double-free if Dest constructor throws (e.g. std::shared_ptr allocation
      // failure).
      else {
        auto* raw = src.release();
        return Dest(raw);
      }
    }
  };

} // namespace frozen

#endif // FROZEN_TRAITS_HPP
