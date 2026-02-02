// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_FSST_DICTIONARY_HPP
#define VAULT_ALGORITHM_FSST_DICTIONARY_HPP

#include <algorithm> // for std::clamp
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
    fsst_dictionary& operator=(const fsst_dictionary&)        = default;
    fsst_dictionary& operator=(fsst_dictionary&&) noexcept    = default;
    ~fsst_dictionary();

    [[nodiscard]] std::optional<std::string> operator[](fsst_key key) const;
    [[nodiscard]] bool                       empty() const;
    [[nodiscard]] std::size_t                size_in_bytes() const;

    // --- Helpers for Key Generation (SSO) ---
    // Exposed so templates can use them, but implementation is in .cpp
    [[nodiscard]] static bool is_inline_candidate(std::string_view s) noexcept;
    [[nodiscard]] static fsst_key make_inline_key(std::string_view s);

    // --- Factories ---

    [[nodiscard]] static fsst_dictionary build(
      std::span<std::string const>  inputs,
      std::function<void(fsst_key)> emit_key,
      sample_ratio                  sample_ratio_ = sample_ratio{1.});

    [[nodiscard]] static fsst_dictionary build(
      std::span<std::string const>  inputs,
      std::function<void(fsst_key)> emit_key,
      compression_level             level);

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build(std::span<std::string const> inputs,
      sample_ratio                     sample_ratio = compression_levels[9]);

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build(std::span<std::string const> inputs, compression_level level);

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build_from_unique(std::span<std::string const> unique_inputs,
      sample_ratio sample_ratio_ = sample_ratio{1.});

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build_from_unique(
      std::span<std::string const> unique_inputs, compression_level level);

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

  // --- Generic Template Interface ---

  template <template <typename,
              typename,
              typename,
              typename,
              typename...> typename MapType = std::unordered_map,
    std::ranges::range R,
    typename Out,
    typename Proj = std::identity>
  [[nodiscard]] auto make_fsst_dictionary(R&& strings,
    Out                                       out,
    Proj                                      proj = {},
    fsst_dictionary::sample_ratio sample_ratio = fsst_dictionary::sample_ratio{
      1.}) -> fsst_dictionary
  {
    // Temporary storage for mapping: Input Index -> (Inline Key OR Large String
    // Index) We use uint64_t to store either the fsst_key.value (if inline) or
    // the index. We can distinguish them because Inline Keys always have the
    // MSB (bit 63) set. Indices (0...N) will have MSB 0 (assuming < 9
    // quintillion strings).
    std::vector<std::uint64_t> key_or_index;

    // We only materialize "Large" strings into the arena.
    // Small strings are converted to keys immediately and discarded.
    auto unique_large_strings = std::vector<std::string>{};

    // Map Large String View -> Unique Index
    auto seen = MapType<std::string_view,
      std::size_t,
      std::hash<std::string_view>,
      std::equal_to<>>{};

    // Helper to estimate size if possible
    if constexpr (requires { std::ranges::size(strings); }) {
      key_or_index.reserve(std::ranges::size(strings));
    }

    for (auto&& s_raw : strings) {
      // Apply projection to get the string-like object, then view it.
      // We do NOT construct a std::string yet.
      auto             projected = std::invoke(proj, s_raw);
      std::string_view sv{projected};

      if (fsst_dictionary::is_inline_candidate(sv)) {
        // CASE A: Small String (SSO)
        // Create key immediately. No heap alloc, no map lookup.
        fsst_key k = fsst_dictionary::make_inline_key(sv);
        key_or_index.push_back(k.value);
      } else {
        // CASE B: Large String
        // Check map.
        auto it = seen.find(sv);
        if (it == seen.end()) {
          // New unique large string.
          // NOW we allocate the std::string copy.
          std::size_t new_idx = unique_large_strings.size();
          unique_large_strings.emplace_back(sv);

          // Add to map. The key points to the stable string in the vector.
          seen.emplace(unique_large_strings.back(), new_idx);

          key_or_index.push_back(new_idx);
        } else {
          // Duplicate large string.
          key_or_index.push_back(it->second);
        }
      }
    }

    // Compress only the large strings
    auto [dict, large_keys] =
      fsst_dictionary::build_from_unique(unique_large_strings, sample_ratio);

    // Emit final sequence
    for (std::uint64_t val : key_or_index) {
      // Check MSB to see if it's an Inline Key or an Index
      if (val & (1UZ << 63)) {
        *out++ = fsst_key{val};
      } else {
        *out++ = large_keys[static_cast<std::size_t>(val)];
      }
    }

    return dict;
  }

  template <template <typename,
              typename,
              typename,
              typename,
              typename...> typename MapType = std::unordered_map,
    std::ranges::range R,
    typename Out,
    typename Proj = std::identity>
  [[nodiscard]] auto make_fsst_dictionary(
    R&& strings, Out out, Proj proj, fsst_dictionary::compression_level level)
    -> fsst_dictionary
  {
    static constexpr auto max_compression_level =
      std::ranges::size(fsst_dictionary::compression_levels);

    level = fsst_dictionary::compression_level{
      std::clamp(level.value, 0uz, max_compression_level - 1)};

    auto sample_ratio = fsst_dictionary::compression_levels[level.value].value;

    return make_fsst_dictionary<MapType>(
      std::forward<R>(strings), out, proj, sample_ratio);
  }

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_FSST_DICTIONARY_HPP
