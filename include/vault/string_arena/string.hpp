#pragma once

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

namespace vault::arena {

  /// \brief The maximum number of bytes that can be stored natively within the
  /// string object without requiring an arena allocation.
  constexpr auto max_inline_size = std::size_t{15};

  /// # string
  /// A highly optimized, immutable string designed to interface with
  /// string arenas.
  ///
  /// It utilizes a 16-byte memory footprint. Strings `max_inline_size` bytes or
  /// shorter are stored entirely inline, making this an owning type for short
  /// strings. Strings larger than `max_inline_size` bytes are stored indirectly
  /// as a 64-bit pointer and a tagged 64-bit size.
  ///
  /// **Constraints**
  /// - Requires a 64-bit little-endian architecture.
  /// - Indirect strings must not exceed 63 bits in length.
  class string {
  public:
    using value_type      = char;
    using pointer         = char const*;
    using const_pointer   = char const*;
    using reference       = char const&;
    using const_reference = char const&;
    using const_iterator  = char const*;
    using iterator        = const_iterator;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    /// Constructs an empty, inline string.
    [[nodiscard]] string() noexcept;

    /// Constructs an arena string from a raw pointer and explicit size.
    ///
    /// - `data`: Pointer to the raw character array.
    /// - `size`: The length of the string.
    [[nodiscard]] string(char const* data, std::size_t size) noexcept;

    /// Constructs an arena string from any type convertible to `std::string_view`.
    ///
    /// - `str`: The input string type.
    template <std::convertible_to<std::string_view> T>
    [[nodiscard]] explicit string(T const& str) noexcept
      : string(std::string_view{str}.data(), std::string_view{str}.size()) {}

    /// Returns a pointer to the underlying character array.
    [[nodiscard]] auto data() const noexcept -> char const*;

    /// Returns the active length of the string.
    [[nodiscard]] auto size() const noexcept -> std::size_t;

    /// Evaluates whether the string is stored entirely within the 16-byte struct.
    [[nodiscard]] auto is_inline() const noexcept -> bool;

    /// Evaluates if the string length is zero.
    [[nodiscard]] auto empty() const noexcept -> bool;

    [[nodiscard]] auto begin() const noexcept -> const_iterator;
    [[nodiscard]] auto end() const noexcept -> const_iterator;
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator;
    [[nodiscard]] auto cend() const noexcept -> const_iterator;

    [[nodiscard]] auto operator[](std::size_t index) const noexcept -> char const&;

    [[nodiscard]] explicit operator std::string_view() const noexcept;

  private:
    struct indirect_data {
      char const* data_;
      std::size_t size_;
    };

    union {
      indirect_data indirect_;
      char          inline_[max_inline_size + 1];
    };

    static auto constexpr indirect_flag_mask = std::size_t{1ULL << 63};
  };

  static_assert(
    std::endian::native == std::endian::little,
    "vault::arena::string relies on little-endian struct layout for the SSO tag."
  );
  static_assert(sizeof(std::size_t) == 8, "vault::arena::string requires a 64-bit size_t.");
  static_assert(sizeof(string) == 16, "vault::arena::string must be exactly 16 bytes.");

  // -----------------------------------------------------------------------------
  // string Inline Definitions
  // -----------------------------------------------------------------------------

  [[nodiscard]] inline string::string() noexcept {
    inline_[0]               = '\0';
    inline_[max_inline_size] = static_cast<char>(max_inline_size);
  }

  [[nodiscard]] inline string::string(char const* data, std::size_t size) noexcept {
    if (size <= max_inline_size) {
      for (auto i = std::size_t{0}; i < size; ++i) {
        inline_[i] = data[i];
      }

      inline_[size]            = '\0';
      inline_[max_inline_size] = static_cast<char>(max_inline_size - size);
    } else {
      assert((size & indirect_flag_mask) == 0 && "String size exceeds 63-bit capacity.");

      indirect_.data_ = data;
      indirect_.size_ = size | indirect_flag_mask;
    }
  }

  [[nodiscard]] inline auto string::is_inline() const noexcept -> bool {
    auto const control_byte = static_cast<unsigned char>(inline_[max_inline_size]);
    return (control_byte & 0x80) == 0;
  }

  [[nodiscard]] inline auto string::size() const noexcept -> std::size_t {
    if (is_inline()) {
      auto const inverted_length = static_cast<std::size_t>(inline_[max_inline_size]);
      return max_inline_size - inverted_length;
    } else {
      return indirect_.size_ & ~indirect_flag_mask;
    }
  }

  [[nodiscard]] inline auto string::data() const noexcept -> char const* {
    if (is_inline()) {
      return inline_;
    } else {
      return indirect_.data_;
    }
  }

  [[nodiscard]] inline auto string::empty() const noexcept -> bool {
    return size() == 0;
  }

  [[nodiscard]] inline auto string::begin() const noexcept -> const_iterator {
    return data();
  }

  [[nodiscard]] inline auto string::end() const noexcept -> const_iterator {
    return data() + size();
  }

  [[nodiscard]] inline auto string::cbegin() const noexcept -> const_iterator {
    return data();
  }

  [[nodiscard]] inline auto string::cend() const noexcept -> const_iterator {
    return data() + size();
  }

  [[nodiscard]] inline auto string::operator[](std::size_t index) const noexcept -> char const& {
    assert(index < size() && "Index out of bounds.");
    return data()[index];
  }

  [[nodiscard]] inline string::operator std::string_view() const noexcept {
    return std::string_view{data(), size()};
  }

  // -----------------------------------------------------------------------------
  // Builder Declarations
  // -----------------------------------------------------------------------------

  /// \brief A type-erased callback used to sink character ranges into the arena.
  using chars_sink = std::function<void(char const* first, char const* last)>;

  /// \brief Constructs an arena of strings from a generator function.
  ///
  /// This function utilizes a two-pass approach to guarantee pointer stability
  /// during arena allocation. All strings larger than `max_inline_size` are
  /// accumulated into a single contiguous shared buffer and explicitly null-terminated.
  /// Short strings are copied locally and instantiate the inline capacity.
  ///
  /// \param chars_source A generator function that accepts a reference to a sink
  ///                     and invokes it exactly once per string.
  /// \return A pair containing the populated vector of arena strings and the
  ///         owning shared memory buffer for any indirect strings.
  [[nodiscard]] auto to_arena(std::function<void(chars_sink&)> chars_source)
    -> std::pair<std::vector<string>, std::shared_ptr<char const[]>>;

  /// \brief Constructs an arena of strings from an input range.
  ///
  /// Delegates directly to the one-pass generator overload.
  ///
  /// \tparam R The type of the input range.
  /// \param range A range whose reference type is convertible to std::string_view.
  /// \return A pair containing the populated vector of arena strings and the
  ///         owning shared memory buffer.
  template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>
  [[nodiscard]] auto to_arena(R&& range) -> std::pair<std::vector<string>, std::shared_ptr<char const[]>> {
    auto chars_source = [&range](chars_sink& sink) {
      for (auto const& item : range) {
        auto const sv = std::string_view{item};
        sink(sv.data(), sv.data() + sv.size());
      }
    };

    return to_arena(std::move(chars_source));
  }

} // namespace vault::arena
