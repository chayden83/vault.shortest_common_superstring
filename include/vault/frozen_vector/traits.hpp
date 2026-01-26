#ifndef FROZEN_TRAITS_HPP
#define FROZEN_TRAITS_HPP

#include <concepts>
#include <memory>
#include <type_traits>

namespace frozen {

  // ============================================================================
  // POINTER TRAITS EXTENSION
  // ============================================================================

  // Concept to detect if a type has a use_count() method
  template <typename T>
  concept HasUseCount = requires(T t) {
    { t.use_count() } -> std::integral;
  };

  /**
   * frozen::pointer_traits
   * Extends std::pointer_traits to add 'is_reference_counted'.
   * Inherits element_type and rebind from standard traits.
   */
  template <typename Ptr> struct pointer_traits : std::pointer_traits<Ptr> {
    // Default Detection: If it looks like a shared pointer, it is one.
    static constexpr bool is_reference_counted = HasUseCount<Ptr>;
  };

  // ============================================================================
  // FREEZE TRAITS (Default Implementation)
  // ============================================================================

  template <typename Src, typename Dest> struct freeze_traits {
    [[nodiscard]]
    static Dest freeze(Src&& src)
    {
      if constexpr (std::is_constructible_v<Dest, Src&&>) {
        return Dest(std::move(src));
      } else {
        auto* raw = src.get();
        Dest  dest(raw);
        src.release();
        return dest;
      }
    }
  };

} // namespace frozen

#endif // FROZEN_TRAITS_HPP
