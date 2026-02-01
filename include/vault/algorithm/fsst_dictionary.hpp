// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_FSST_COMPRESSION_HPP
#define VAULT_ALGORITHM_FSST_COMPRESSION_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

namespace vault::algorithm {

  struct fsst_key {
    std::size_t offset;
    std::size_t length;
    bool        operator==(const fsst_key&) const = default;
  };

  class fsst_dictionary {
  public:
    fsst_dictionary();
    ~fsst_dictionary();

    fsst_dictionary(fsst_dictionary&&) noexcept;
    fsst_dictionary& operator=(fsst_dictionary&&) noexcept;

    fsst_dictionary(const fsst_dictionary&)            = delete;
    fsst_dictionary& operator=(const fsst_dictionary&) = delete;

    [[nodiscard]] std::optional<std::string> operator[](fsst_key key) const;
    [[nodiscard]] bool                       empty() const;
    [[nodiscard]] std::size_t                size_in_bytes() const;

    /**
     * @brief Core factory method. Builds a dictionary and emits keys via
     * callback.
     * @param inputs The raw string data to compress.
     * @param emit_key Callback invoked for each input string with its
     * corresponding fsst_key.
     * @return The compressed dictionary.
     */
    static fsst_dictionary build(const std::vector<std::string>& inputs,
      std::function<void(fsst_key)>                              emit_key);

    /**
     * @brief Convenience factory method. Returns keys as a vector.
     * Delegates to the callback-based overload.
     * @param inputs The raw string data to compress.
     * @return A pair containing the compressed dictionary and the vector of
     * keys.
     */
    static std::pair<fsst_dictionary, std::vector<fsst_key>> build(
      const std::vector<std::string>& inputs);

  private:
    class impl;
    std::unique_ptr<impl> pImpl;

    explicit fsst_dictionary(std::unique_ptr<impl> implementation);
  };

  // --- Generic Template Interface ---

  template <std::ranges::range R, typename Out, typename Proj = std::identity>
  auto fsst_compress_strings(R&& strings, Out out, Proj proj = {})
    -> fsst_dictionary
  {
    auto working_set = strings | ::ranges::views::transform([&](auto&& s) {
      return std::string(std::invoke(proj, s));
    }) | ::ranges::to<std::vector<std::string>>();

    if (working_set.empty()) {
      return fsst_dictionary{};
    }

    return fsst_dictionary::build(
      working_set, [&](fsst_key key) { *out++ = key; });
  }

} // namespace vault::algorithm

#endif // VAULT_ALGORITHM_FSST_COMPRESSION_HPP
