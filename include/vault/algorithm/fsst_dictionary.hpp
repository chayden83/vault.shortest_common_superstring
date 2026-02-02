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
#include <variant>
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
    using Generator = std::function<std::optional<std::string>()>;
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
    fsst_dictionary& operator=(const fsst_dictionary&)        = default;
    fsst_dictionary& operator=(fsst_dictionary&&) noexcept    = default;
    ~fsst_dictionary();

    [[nodiscard]] std::optional<std::string> operator[](fsst_key key) const;
    [[nodiscard]] bool                       empty() const;
    [[nodiscard]] std::size_t                size_in_bytes() const;

    // --- Helpers for Key Generation (SSO) ---
    [[nodiscard]] static bool is_inline_candidate(std::string_view s) noexcept;
    [[nodiscard]] static fsst_key make_inline_key(std::string_view s);

    // --- Factories ---

    /**
     * @brief Type-erased build function.
     * * @param gen Generates strings one by one. Returns nullopt when done.
     * @param dedup Maps a string to a unique index.
     * Returns {index, true} if the string is new.
     * Returns {index, false} if the string was seen before.
     * @param emit_key Callback to receive the final keys in order.
     * @param sample_ratio Training sample ratio.
     */
    [[nodiscard]] static fsst_dictionary build(Generator gen,
      Deduplicator                                       dedup,
      std::function<void(fsst_key)>                      emit_key,
      sample_ratio ratio = sample_ratio{1.});

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
    // 1. Prepare Generator
    auto it  = std::ranges::begin(strings);
    auto end = std::ranges::end(strings);

    auto generator = [it, end, proj]() mutable -> std::optional<std::string> {
      if (it == end) {
        return std::nullopt;
      }
      // Invoke projection and convert to string
      std::string s(std::invoke(proj, *it));
      ++it;
      return s;
    };

    // 2. Prepare Deduplicator
    // The map stores string_view -> index.
    // Note: The map keys must point to stable storage. The core 'build'
    // function in the .cpp file will ensure strings are stored stably before
    // calling this.
    auto seen = MapType<std::string_view,
      std::size_t,
      std::hash<std::string_view>,
      std::equal_to<>>{};

    // Hint size if possible
    if constexpr (requires { std::ranges::size(strings); }) {
      if constexpr (requires { seen.reserve(1); }) {
        seen.reserve(std::ranges::size(strings));
      }
    }

    std::size_t next_index = 0;
    auto        deduplicator =
      [&](std::string_view sv) -> std::pair<std::uint64_t, bool> {
      auto [iter, inserted] = seen.emplace(sv, next_index);

      if (inserted) {
        return {next_index++, true};
      }
      return {iter->second, false};
    };

    // 3. Delegate to core implementation
    return fsst_dictionary::build(
      generator, deduplicator, [&](fsst_key k) { *out++ = k; }, sample_ratio);
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

    auto sample_ratio = fsst_dictionary::compression_levels[level.value];

    return make_fsst_dictionary<MapType>(
      std::forward<R>(strings), out, proj, sample_ratio);
  }

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_FSST_DICTIONARY_HPP
