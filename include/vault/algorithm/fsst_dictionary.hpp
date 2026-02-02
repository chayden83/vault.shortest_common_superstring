// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_FSST_DICTIONARY_HPP
#define VAULT_ALGORITHM_FSST_DICTIONARY_HPP

#include <algorithm> // for std::clamp
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

  struct fsst_key {
    std::uint64_t value;
    bool          operator==(const fsst_key&) const = default;
  };

  static_assert(sizeof(fsst_key) == 8, "fsst_key must be exactly 8 bytes");

  struct fsst_dictionary {
    struct sample_ratio {
      float value = 1.0;
    };

    struct compression_level {
      std::size_t value = 9;
    };

    // Zero-Copy Generator: Returns a view into existing, stable memory.
    using Generator = std::function<std::optional<std::string_view>()>;

    // R-Value Generator: Returns a string (ownership transfer).
    using RValueGenerator = std::function<std::optional<std::string>()>;

    using Deduplicator =
      std::function<std::pair<std::uint64_t, bool>(std::string_view)>;

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

    [[nodiscard]] fsst_dictionary();

    [[nodiscard]] fsst_dictionary(const fsst_dictionary&)     = default;
    [[nodiscard]] fsst_dictionary(fsst_dictionary&&) noexcept = default;

    fsst_dictionary& operator=(const fsst_dictionary&)     = default;
    fsst_dictionary& operator=(fsst_dictionary&&) noexcept = default;

    ~fsst_dictionary();

    [[nodiscard]] std::optional<std::string> operator[](fsst_key key) const;
    [[nodiscard]] bool                       empty() const;
    [[nodiscard]] std::size_t                size_in_bytes() const;

    [[nodiscard]] static bool is_inline_candidate(std::string_view s) noexcept;
    [[nodiscard]] static fsst_key make_inline_key(std::string_view s);

    [[nodiscard]] static constexpr inline sample_ratio level_to_ratio(
      compression_level level)
    {
      static constexpr auto max_compression_level =
        std::ranges::size(fsst_dictionary::compression_levels);

      level = fsst_dictionary::compression_level{
        std::clamp(level.value, 0uz, max_compression_level - 1)};

      return fsst_dictionary::compression_levels[level.value];
    }

    // --- Core Factories (View Based) ---
    [[nodiscard]] static fsst_dictionary build(Generator gen,
      Deduplicator                                       dedup,
      std::function<void(fsst_key)>                      emit_key,
      sample_ratio ratio = sample_ratio{1.});

    [[nodiscard]] static fsst_dictionary build_from_unique(Generator gen,
      std::function<void(fsst_key)>                                  emit_key,
      sample_ratio ratio = sample_ratio{1.});

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build(
      Generator gen, Deduplicator dedup, sample_ratio ratio = sample_ratio{1.});

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build_from_unique(Generator gen, sample_ratio ratio = sample_ratio{1.});

    // --- R-Value Factories (String Based) ---
    [[nodiscard]] static fsst_dictionary build(RValueGenerator gen,
      Deduplicator                                             dedup,
      std::function<void(fsst_key)>                            emit_key,
      sample_ratio ratio = sample_ratio{1.});

    [[nodiscard]] static fsst_dictionary build_from_unique(RValueGenerator gen,
      std::function<void(fsst_key)> emit_key,
      sample_ratio                  ratio = sample_ratio{1.});

    // --- Range Interface (Generalized) ---
    template <std::ranges::input_range R, typename... Args>
    [[nodiscard]] static auto build(R&& range, Args&&... args)
    {
      using ValueType = std::ranges::range_value_t<R>;
      using Reference = std::ranges::range_reference_t<R>;

      constexpr bool is_contiguous_lvalue =
        std::ranges::contiguous_range<ValueType>
        && std::is_lvalue_reference_v<Reference>;

      auto it  = std::ranges::begin(range);
      auto end = std::ranges::end(range);

      if constexpr (is_contiguous_lvalue) {
        // --- Zero Copy Path ---
        Generator gen = [it, end]() mutable -> std::optional<std::string_view> {
          if (it == end) {
            return std::nullopt;
          }
          auto&& val = *it;
          ++it;
          return std::string_view{
            reinterpret_cast<const char*>(std::ranges::data(val)),
            std::ranges::size(val)};
        };
        return build(std::move(gen), std::forward<Args>(args)...);

      } else {
        // --- R-Value / Non-Contiguous Path --- We project the
        // element to std::string and let the source file handle
        // buffering.
        RValueGenerator gen = [it,
                                end]() mutable -> std::optional<std::string> {
          if (it == end) {
            return std::nullopt;
          }
          auto&& val = *it;
          // Construct string from the byte range
          std::string s(reinterpret_cast<const char*>(std::ranges::data(val)),
            std::ranges::size(val));
          ++it;
          return s;
        };
        return build(std::move(gen), std::forward<Args>(args)...);
      }
    }

  private:
    class impl;
    std::shared_ptr<impl const> p_impl;

    [[nodiscard]] explicit fsst_dictionary(
      std::shared_ptr<impl const> implementation);

    [[nodiscard]] static std::pair<std::shared_ptr<impl>, std::vector<fsst_key>>
    compress_core(std::size_t count,
      std::size_t*            lens,
      unsigned char const**   ptrs,
      float                   sample_ratio);
  };
} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_FSST_DICTIONARY_HPP
