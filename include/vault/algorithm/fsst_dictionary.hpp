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

  // Opaque handle.
  // Internal bit layout is an implementation detail defined in the source file.
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

    // Callback types for type-erased construction
    using RValueGenerator = std::function<std::optional<std::string>()>;
    using LValueGenerator = std::function<std::string const*()>;

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

    // --- Helpers for Key Generation (SSO) ---
    [[nodiscard]] static bool is_inline_candidate(std::string_view s) noexcept;
    [[nodiscard]] static fsst_key make_inline_key(std::string_view s);

    // --- Helper for Level to Ratio Conversion ---
    [[nodiscard]] static constexpr inline sample_ratio level_to_ratio(
      compression_level level)
    {
      static constexpr auto max_compression_level =
        std::ranges::size(fsst_dictionary::compression_levels);

      level = fsst_dictionary::compression_level{
        std::clamp(level.value, 0uz, max_compression_level - 1)};

      return fsst_dictionary::compression_levels[level.value];
    }

    // --- Factories ---
    [[nodiscard]] static fsst_dictionary build(LValueGenerator gen,
      Deduplicator                                             dedup,
      std::function<void(fsst_key)>                            emit_key,
      sample_ratio ratio = sample_ratio{1.});

    [[nodiscard]] static fsst_dictionary build_from_unique(LValueGenerator gen,
      std::function<void(fsst_key)> emit_key,
      sample_ratio                  ratio = sample_ratio{1.});

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build(LValueGenerator gen,
      Deduplicator        dedup,
      sample_ratio        ratio = sample_ratio{1.});

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build_from_unique(
      LValueGenerator gen, sample_ratio ratio = sample_ratio{1.});

    [[nodiscard]] static inline fsst_dictionary build(LValueGenerator gen,
      Deduplicator                                                    dedup,
      std::function<void(fsst_key)>                                   emit_key,
      compression_level                                               level)
    {
      return build(std::move(gen),
        std::move(dedup),
        std::move(emit_key),
        level_to_ratio(level));
    }

    [[nodiscard]] static inline fsst_dictionary build_from_unique(
      LValueGenerator               gen,
      std::function<void(fsst_key)> emit_key,
      compression_level             level)
    {
      return build_from_unique(
        std::move(gen), std::move(emit_key), level_to_ratio(level));
    }

    [[nodiscard]] static inline std::pair<fsst_dictionary,
      std::vector<fsst_key>>
    build(LValueGenerator gen, Deduplicator dedup, compression_level level)
    {
      return build(std::move(gen), std::move(dedup), level_to_ratio(level));
    }

    [[nodiscard]] static inline std::pair<fsst_dictionary,
      std::vector<fsst_key>>
    build_from_unique(LValueGenerator gen, compression_level level)
    {
      return build_from_unique(std::move(gen), level_to_ratio(level));
    }

    template <typename... Args>
    [[nodiscard]] static auto build(RValueGenerator gen, Args&&... args)
    {
      auto strings = std::vector<std::string>{};

      for (auto string = std::invoke(gen); string.has_value();
        string         = std::invoke(gen)) {
        strings.emplace_back(*std::move(string));
      }

      auto first = std::ranges::begin(strings);
      auto last  = std::ranges::end(strings);

      auto lgen = [=]() mutable -> std::string const* {
        return first != last ? std::addressof(*first++) : nullptr;
      };

      return build(std::move(lgen), std::forward<Args>(args)...);
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
