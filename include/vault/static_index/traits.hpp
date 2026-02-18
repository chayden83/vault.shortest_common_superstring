#ifndef VAULT_STATIC_INDEX_TRAITS_HPP
#define VAULT_STATIC_INDEX_TRAITS_HPP

#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>

namespace vault::containers::traits {
  // --- Primary Template ---
  template <typename T>
  struct underlying_byte_sequences;
} // namespace vault::containers::traits

namespace vault::containers::concepts {

  // --- Concept: Byte Visitor ---
  // A callable that accepts a span of const bytes.
  template <typename F>
  concept byte_sequence_visitor = requires(F f, std::span<std::byte const> b) {
    { f(b) } -> std::same_as<void>;
  };

  // --- Helper Concept to check if T is hashable ---
  template <typename T>
  concept underlying_byte_sequences = requires(const T& t) {
    traits::underlying_byte_sequences<std::remove_cvref_t<T>>::visit(t, [](std::span<std::byte const>) {});
  };
} // namespace vault::containers::concepts

namespace vault::containers::traits {
  // --- Specialization 1: Fundamental Types (int, float, char, etc.) ---
  template <typename T>
    requires std::is_fundamental_v<T>
  struct underlying_byte_sequences<T> {
    template <concepts::byte_sequence_visitor V>
    static void visit(const T& val, V&& v) {
      v(std::as_bytes(std::span(&val, 1)));
    }
  };

  // --- Specialization 2: Contiguous Ranges of Fundamental Types ---
  //
  // Matches: std::string, std::string_view, std::vector<int>,
  // std::array<float> Does NOT Match: std::vector<std::string>
  // (value_type is not fundamental)
  template <typename T>
    requires std::ranges::contiguous_range<T> && std::is_fundamental_v<std::ranges::range_value_t<T>>
  struct underlying_byte_sequences<T> {
    template <concepts::byte_sequence_visitor V>
    static void visit(const T& range, V&& v) {
      // Optimization: Hash the whole block at once
      v(std::as_bytes(std::span(range)));
    }
  };

  // --- Specialization 3: General Iterable Ranges (Recursive) ---
  //
  // Matches: std::vector<std::string>, std::list<int>,
  // std::vector<MyStruct> Action: Recursively invokes
  // underlying_byte_sequences on each element.
  template <typename T>
    requires std::ranges::input_range<T> &&
             (!std::ranges::contiguous_range<T> || !std::is_fundamental_v<std::ranges::range_value_t<T>>)
  struct underlying_byte_sequences<T> {
    template <concepts::byte_sequence_visitor V>
    static void visit(const T& range, V&& v) {
      for (const auto& element : range) {
        // Recursion: Let the trait decide how to handle the element
        underlying_byte_sequences<std::ranges::range_value_t<T>>::visit(element, v);
      }
    }
  };
} // namespace vault::containers::traits

#endif
