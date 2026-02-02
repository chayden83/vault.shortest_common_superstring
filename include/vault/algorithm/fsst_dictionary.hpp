// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_FSST_DICTIONARY_HPP
#define VAULT_ALGORITHM_FSST_DICTIONARY_HPP

#include <algorithm> // for std::clamp
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

namespace vault::algorithm {

  /// @brief A lightweight, opaque handle to a string stored in an
  /// fsst_dictionary.
  ///
  /// @details
  /// An fsst_key is an 8-byte value that encodes the location of a string.
  /// It utilizes a "Small String Optimization" (SSO) scheme:
  /// - **Inline:** If the string is <= 7 bytes, the characters and length are
  ///   packed directly into the 64-bit integer. Dereferencing is effectively
  ///   free.
  /// - **Pointer:** If the string is > 7 bytes, the key stores an offset into
  ///   the dictionary's compressed data blob and the compressed length.
  struct fsst_key {
    std::uint64_t value;
    bool          operator==(const fsst_key&) const = default;
  };

  static_assert(sizeof(fsst_key) == 8, "fsst_key must be exactly 8 bytes");

  /// @brief Base class for FSST dictionary implementation.
  ///
  /// @details
  /// Holds the compressed data and provides the core decompression logic.
  /// Users should generally use the `fsst_dictionary<ByteContainer>` template
  /// to ensure type-safe return values.
  class fsst_dictionary_base {
  public:
    /// @brief Configuration for the compression training sample rate.
    /// Values must be in (0.0, 1.0]. Lower values speed up construction for
    /// large datasets.
    struct sample_ratio {
      float value = 1.0;
    };

    /// @brief Abstract configuration for compression effort (0-9).
    /// Level 9 implies 100% sampling; Level 0 implies 1/1024 sampling.
    struct compression_level {
      std::size_t value = 9;
    };

    /// @brief Generator for Views (Zero-Copy).
    /// @details
    /// Returns a view into **stable, existing memory**.
    /// - `std::nullopt` signals the end of the sequence.
    /// - The memory pointed to by the `string_view` must remain valid until
    /// `build` returns.
    using Generator = std::function<std::optional<std::string_view>()>;

    /// @brief Generator for R-Values (Ownership Transfer).
    /// @details
    /// Returns a string that the dictionary builder must take ownership of.
    /// - The builder will move these strings into an internal stable buffer
    /// before processing.
    /// - `std::nullopt` signals the end of the sequence.
    using RValueGenerator = std::function<std::optional<std::string>()>;

    /// @brief Deduplication callback strategy.
    /// @param s The string to look up or insert.
    /// @return A pair `{index, inserted}`. If `inserted` is true, the string is
    /// new.
    using Deduplicator =
      std::function<std::pair<std::uint64_t, bool>(std::string_view)>;

    /// @brief Pre-defined sampling ratios corresponding to compression levels
    /// 0-9.
    static constexpr inline sample_ratio const compression_levels[10] = {
      {1.0f / 1024.0f},
      {1.0f / 512.0f},
      {1.0f / 256.0f},
      {1.0f / 128.0f},
      {1.0f / 64.0f},
      {1.0f / 32.0f},
      {1.0f / 16.0f},
      {1.0f / 8.0f},
      {1.0f / 4.0f},
      {1.0f}};

    [[nodiscard]] fsst_dictionary_base();

    [[nodiscard]] fsst_dictionary_base(const fsst_dictionary_base&) = default;
    [[nodiscard]] fsst_dictionary_base(
      fsst_dictionary_base&&) noexcept = default;

    fsst_dictionary_base& operator=(const fsst_dictionary_base&)     = default;
    fsst_dictionary_base& operator=(fsst_dictionary_base&&) noexcept = default;

    virtual ~fsst_dictionary_base();

    [[nodiscard]] bool        empty() const;
    [[nodiscard]] std::size_t size_in_bytes() const;

    // --- Helpers for Key Generation ---

    /// @brief Checks if a string is small enough to fit inline in an fsst_key.
    /// @return true if s.size() <= 7.
    [[nodiscard]] static bool is_inline_candidate(std::string_view s) noexcept;

    /// @brief Creates an inline fsst_key from a small string.
    /// @pre s.size() <= 7
    /// @throws std::length_error if s.size() > 7.
    [[nodiscard]] static fsst_key make_inline_key(std::string_view s);

    /// @brief Converts an integer compression level (0-9) to a sampling ratio.
    [[nodiscard]] static constexpr inline auto level_to_ratio(
      compression_level level) -> sample_ratio
    {
      static constexpr auto max_compression_level =
        std::ranges::size(fsst_dictionary_base::compression_levels);

      auto const clamped_level =
        std::clamp(level.value, 0uz, max_compression_level - 1);

      return fsst_dictionary_base::compression_levels[clamped_level];
    }

    // --- Generic Lookup API ---

    /// @brief Tries to find and decompress the value for `key` into an existing
    /// container.
    /// @tparam ByteContainer A vector-like container (resize, data, size).
    /// @param dict The dictionary instance.
    /// @param key The key to look up.
    /// @param out The container to write into. Its content will be overwritten.
    /// @return true if found, false if key is invalid.
    /// @note This function reuses `out`'s existing capacity if sufficient,
    /// avoiding allocations.
    template <typename ByteContainer>
    friend inline auto try_find(
      fsst_dictionary_base const& dict, fsst_key key, ByteContainer& out)
      -> bool
    {
      auto limit = dict.get_conservative_length(key);
      if (limit == 0 && dict.empty() && !dict.is_inline_key_internal(key)) {
        return false;
      }

      // Reuse capacity if possible
      if (std::ranges::size(out) < limit) {
        out.resize(limit);
      }

      auto const res = dict.decompress_into(
        key, static_cast<void*>(std::ranges::data(out)), limit);

      if (res == -2) {
        return false; // Not Found
      }

      // Invariant: Conservative estimate must be sufficient
      assert(res >= 0 && "Conservative memory estimate was insufficient");

      out.resize(static_cast<std::size_t>(res));
      return true;
    }

    /// @brief Tries to find and decompress the value for `key` into a new
    /// container.
    /// @tparam ByteContainer A container type (e.g., std::string,
    /// std::vector<byte>).
    /// @param dict The dictionary instance.
    /// @param key The key to look up.
    /// @return The decompressed value, or std::nullopt if the key is invalid.
    template <typename ByteContainer>
    friend inline auto try_find(fsst_dictionary_base const& dict, fsst_key key)
      -> std::optional<ByteContainer>
    {
      auto container = ByteContainer();
      if (try_find(dict, key, container)) {
        return container;
      }
      return std::nullopt;
    }

    // --- Core Factories (View Based) ---

    [[nodiscard]] static auto build(Generator gen,
      Deduplicator                            dedup,
      std::function<void(fsst_key)>           emit_key,
      sample_ratio ratio = sample_ratio{1.}) -> fsst_dictionary_base;

    [[nodiscard]] static auto build_from_unique(Generator gen,
      std::function<void(fsst_key)>                       emit_key,
      sample_ratio ratio = sample_ratio{1.}) -> fsst_dictionary_base;

    [[nodiscard]] static auto build(
      Generator gen, Deduplicator dedup, sample_ratio ratio = sample_ratio{1.})
      -> std::pair<fsst_dictionary_base, std::vector<fsst_key>>;

    [[nodiscard]] static auto build_from_unique(
      Generator gen, sample_ratio ratio = sample_ratio{1.})
      -> std::pair<fsst_dictionary_base, std::vector<fsst_key>>;

    // --- R-Value Factories (String Based) ---

    [[nodiscard]] static auto build(RValueGenerator gen,
      Deduplicator                                  dedup,
      std::function<void(fsst_key)>                 emit_key,
      sample_ratio ratio = sample_ratio{1.}) -> fsst_dictionary_base;

    [[nodiscard]] static auto build_from_unique(RValueGenerator gen,
      std::function<void(fsst_key)>                             emit_key,
      sample_ratio ratio = sample_ratio{1.}) -> fsst_dictionary_base;

    [[nodiscard]] static auto build(RValueGenerator gen,
      Deduplicator                                  dedup,
      sample_ratio                                  ratio = sample_ratio{1.})
      -> std::pair<fsst_dictionary_base, std::vector<fsst_key>>;

    [[nodiscard]] static auto build_from_unique(
      RValueGenerator gen, sample_ratio ratio = sample_ratio{1.})
      -> std::pair<fsst_dictionary_base, std::vector<fsst_key>>;

    // --- Compression Level Overloads ---

    [[nodiscard]] static inline auto build(Generator gen,
      Deduplicator                                   dedup,
      std::function<void(fsst_key)>                  emit_key,
      compression_level level) -> fsst_dictionary_base
    {
      return build(std::move(gen),
        std::move(dedup),
        std::move(emit_key),
        level_to_ratio(level));
    }

    [[nodiscard]] static inline auto build_from_unique(Generator gen,
      std::function<void(fsst_key)>                              emit_key,
      compression_level level) -> fsst_dictionary_base
    {
      return build_from_unique(
        std::move(gen), std::move(emit_key), level_to_ratio(level));
    }

    [[nodiscard]] static inline auto build(
      Generator gen, Deduplicator dedup, compression_level level)
      -> std::pair<fsst_dictionary_base, std::vector<fsst_key>>
    {
      return build(std::move(gen), std::move(dedup), level_to_ratio(level));
    }

    [[nodiscard]] static inline auto build_from_unique(
      Generator gen, compression_level level)
      -> std::pair<fsst_dictionary_base, std::vector<fsst_key>>
    {
      return build_from_unique(std::move(gen), level_to_ratio(level));
    }

    // --- Range Interface (Generalized) ---

    /// @brief Generic build from any Input Range.
    template <std::ranges::input_range R, typename... Args>
    [[nodiscard]] static auto build(R&& range, Args&&... args)
    {
      using ValueType = std::ranges::range_value_t<R>;
      using Reference = std::ranges::range_reference_t<R>;

      constexpr auto is_contiguous_lvalue =
        std::ranges::contiguous_range<ValueType>
        && std::is_lvalue_reference_v<Reference>;

      auto it  = std::ranges::begin(range);
      auto end = std::ranges::end(range);

      if constexpr (is_contiguous_lvalue) {
        auto gen =
          Generator{[it, end]() mutable -> std::optional<std::string_view> {
            if (it == end) {
              return std::nullopt;
            }
            auto&& val = *it;
            ++it;
            return std::string_view{
              reinterpret_cast<const char*>(std::ranges::data(val)),
              std::ranges::size(val)};
          }};
        return build(std::move(gen), std::forward<Args>(args)...);

      } else {
        auto gen =
          RValueGenerator{[it, end]() mutable -> std::optional<std::string> {
            if (it == end) {
              return std::nullopt;
            }
            auto&& val = *it;
            auto   s   = std::string();
            if constexpr (std::ranges::contiguous_range<ValueType>) {
              s.assign(reinterpret_cast<const char*>(std::ranges::data(val)),
                std::ranges::size(val));
            } else {
              for (auto c : val) {
                s.push_back(static_cast<char>(c));
              }
            }
            ++it;
            return s;
          }};
        return build(std::move(gen), std::forward<Args>(args)...);
      }
    }

    /// @brief Generic build from unique Input Range.
    template <std::ranges::input_range R, typename... Args>
    [[nodiscard]] static auto build_from_unique(R&& range, Args&&... args)
    {
      using ValueType = std::ranges::range_value_t<R>;
      using Reference = std::ranges::range_reference_t<R>;

      constexpr auto is_contiguous_lvalue =
        std::ranges::contiguous_range<ValueType>
        && std::is_lvalue_reference_v<Reference>;

      auto it  = std::ranges::begin(range);
      auto end = std::ranges::end(range);

      if constexpr (is_contiguous_lvalue) {
        auto gen =
          Generator{[it, end]() mutable -> std::optional<std::string_view> {
            if (it == end) {
              return std::nullopt;
            }
            auto&& val = *it;
            ++it;
            return std::string_view{
              reinterpret_cast<const char*>(std::ranges::data(val)),
              std::ranges::size(val)};
          }};
        return build_from_unique(std::move(gen), std::forward<Args>(args)...);

      } else {
        auto gen =
          RValueGenerator{[it, end]() mutable -> std::optional<std::string> {
            if (it == end) {
              return std::nullopt;
            }
            auto&& val = *it;
            auto   s   = std::string();
            if constexpr (std::ranges::contiguous_range<ValueType>) {
              s.assign(reinterpret_cast<const char*>(std::ranges::data(val)),
                std::ranges::size(val));
            } else {
              for (auto c : val) {
                s.push_back(static_cast<char>(c));
              }
            }
            ++it;
            return s;
          }};
        return build_from_unique(std::move(gen), std::forward<Args>(args)...);
      }
    }

  private:
    class impl;
    std::shared_ptr<impl const> p_impl;

    [[nodiscard]] explicit fsst_dictionary_base(
      std::shared_ptr<impl const> implementation);

    [[nodiscard]] static auto compress_core(std::size_t count,
      std::size_t*                                      lens,
      unsigned char const**                             ptrs,
      float                                             sample_ratio)
      -> std::pair<std::shared_ptr<impl>, std::vector<fsst_key>>;

    // --- Private Helpers for Friend Access ---

    [[nodiscard]] auto get_conservative_length(fsst_key k) const -> std::size_t;

    [[nodiscard]] auto decompress_into(
      fsst_key k, void* dst, std::size_t capacity) const -> std::ptrdiff_t;

    [[nodiscard]] auto is_inline_key_internal(fsst_key k) const -> bool;
  };

  /// @brief Typed wrapper for FSST dictionary.
  /// @tparam ByteContainer Type returned by operator[] (e.g. std::string).
  template <typename ByteContainer>
  class fsst_dictionary : public fsst_dictionary_base {
  public:
    using fsst_dictionary_base::fsst_dictionary_base;

    /// @brief Explicit conversion from base.
    explicit fsst_dictionary(fsst_dictionary_base&& base)
        : fsst_dictionary_base(std::move(base))
    {}

    /// @brief Decompresses and returns the value as ByteContainer.
    [[nodiscard]] auto operator[](fsst_key key) const
      -> std::optional<ByteContainer>
    {
      return try_find<ByteContainer>(*this, key);
    }

    // --- Shadow Factories (Return fsst_dictionary<T>) ---

    template <typename... Args> [[nodiscard]] static auto build(Args&&... args)
    {
      // Check if the base build returns a pair or just the dictionary
      using BaseResult =
        decltype(fsst_dictionary_base::build(std::forward<Args>(args)...));

      if constexpr (std::is_same_v<BaseResult, fsst_dictionary_base>) {
        return fsst_dictionary(
          fsst_dictionary_base::build(std::forward<Args>(args)...));
      } else {
        // It's a pair<fsst_dictionary_base, vector<fsst_key>>
        auto [base, keys] =
          fsst_dictionary_base::build(std::forward<Args>(args)...);
        return std::make_pair(
          fsst_dictionary(std::move(base)), std::move(keys));
      }
    }

    template <typename... Args>
    [[nodiscard]] static auto build_from_unique(Args&&... args)
    {
      using BaseResult = decltype(fsst_dictionary_base::build_from_unique(
        std::forward<Args>(args)...));

      if constexpr (std::is_same_v<BaseResult, fsst_dictionary_base>) {
        return fsst_dictionary(
          fsst_dictionary_base::build_from_unique(std::forward<Args>(args)...));
      } else {
        auto [base, keys] =
          fsst_dictionary_base::build_from_unique(std::forward<Args>(args)...);
        return std::make_pair(
          fsst_dictionary(std::move(base)), std::move(keys));
      }
    }
  };

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_FSST_DICTIONARY_HPP
