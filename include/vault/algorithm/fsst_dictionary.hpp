// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_FSST_DICTIONARY_HPP
#define VAULT_ALGORITHM_FSST_DICTIONARY_HPP

#include <cstddef>
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

  struct fsst_key {
    std::size_t offset : 40;
    std::size_t length : 24;

    bool operator==(const fsst_key&) const = default;
  };

  static_assert(sizeof(fsst_key) == 8, "fsst_key must be exactly 8 bytes");

  class fsst_dictionary {
  public:
    [[nodiscard]] fsst_dictionary();
    [[nodiscard]] fsst_dictionary(const fsst_dictionary&)     = default;
    [[nodiscard]] fsst_dictionary(fsst_dictionary&&) noexcept = default;
    fsst_dictionary& operator=(const fsst_dictionary&)        = default;
    fsst_dictionary& operator=(fsst_dictionary&&) noexcept    = default;
    ~fsst_dictionary();

    [[nodiscard]] std::optional<std::string> operator[](fsst_key key) const;
    [[nodiscard]] bool                       empty() const;
    [[nodiscard]] std::size_t                size_in_bytes() const;

    [[nodiscard]] static fsst_dictionary build(
      std::span<std::string const>  inputs,
      std::function<void(fsst_key)> emit_key);

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build(std::span<std::string const> inputs);

    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build_from_unique(std::span<std::string const> unique_inputs);

  private:
    class impl;
    std::shared_ptr<impl const> p_impl;

    [[nodiscard]] explicit fsst_dictionary(
      std::shared_ptr<impl const> implementation);

    // Private helper to perform the raw FSST compression.
    // Being a member function allows it to access the private 'impl' class.
    [[nodiscard]] static std::pair<std::shared_ptr<impl>, std::vector<fsst_key>>
    compress_core(
      std::size_t count, std::size_t* lens, unsigned char const** ptrs);
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
  [[nodiscard]] auto make_fsst_dictionary(R&& strings, Out out, Proj proj = {})
    -> fsst_dictionary
  {
    auto input_strings = strings | ::ranges::views::transform([&](auto&& s) {
      return std::string(std::invoke(proj, s));
    }) | ::ranges::to<std::vector<std::string>>();

    if (input_strings.empty()) {
      return fsst_dictionary{};
    }

    auto seen = MapType<std::string_view,
      std::size_t,
      std::hash<std::string_view>,
      std::equal_to<>>{};

    if constexpr (requires { seen.reserve(1); }) {
      seen.reserve(input_strings.size());
    }

    auto unique_strings = std::vector<std::string>{};
    unique_strings.reserve(input_strings.size());

    auto input_to_unique = std::vector<std::size_t>{};
    input_to_unique.reserve(input_strings.size());

    for (const auto& s : input_strings) {
      auto [it, inserted] =
        seen.emplace(std::string_view{s}, unique_strings.size());
      if (inserted) {
        unique_strings.push_back(s);
      }
      input_to_unique.push_back(it->second);
    }

    auto [dict, unique_keys] =
      fsst_dictionary::build_from_unique(unique_strings);

    for (auto unique_idx : input_to_unique) {
      *out++ = unique_keys[unique_idx];
    }

    return dict;
  }

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_FSST_DICTIONARY_HPP
