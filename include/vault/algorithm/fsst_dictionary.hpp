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
#include <utility>
#include <vector>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

namespace vault::algorithm {

  /**
   * @brief A compressed key representing a reference to a string stored within
   * an fsst_dictionary.
   *
   * @details
   * The key is a packed 64-bit structure containing both the offset into the
   * compressed blob and the length of the compressed data.
   *
   * - Offset: 40 bits (Max addressable size: 1 TB)
   * - Length: 24 bits (Max string size: 16 MB)
   *
   * This structure is trivially copyable and is designed to be passed by value.
   */
  struct fsst_key {
    /// The byte offset into the compressed data blob.
    std::size_t offset : 40;
    /// The length of the compressed string data in bytes.
    std::size_t length : 24;

    /// Default equality comparison.
    bool operator==(const fsst_key&) const = default;
  };

  static_assert(sizeof(fsst_key) == 8, "fsst_key must be exactly 8 bytes");

  /**
   * @brief A read-only, compressed dictionary of strings.
   *
   * @details
   * The fsst_dictionary stores a collection of strings compressed using the
   * Fast Static Symbol Table (FSST) algorithm. It provides random access to
   * strings via @ref fsst_key handles.
   *
   * The dictionary is immutable once built. It supports efficient copy and
   * move operations by sharing the underlying read-only compressed data via
   * reference counting.
   *
   * @note
   * - Max individual string size: 16 MB
   * - Max total dictionary size: 1 TB
   */
  class fsst_dictionary {
  public:
    /**
     * @brief Constructs an empty dictionary.
     */
    [[nodiscard]] fsst_dictionary();

    /**
     * @brief Copy constructor.
     * @details Shares ownership of the underlying compressed data (cheap).
     */
    [[nodiscard]] fsst_dictionary(const fsst_dictionary&) = default;

    /**
     * @brief Move constructor.
     * @details Transfers ownership of the underlying compressed data.
     */
    [[nodiscard]] fsst_dictionary(fsst_dictionary&&) noexcept = default;

    /**
     * @brief Copy assignment operator.
     * @details Shares ownership of the underlying compressed data.
     */
    fsst_dictionary& operator=(const fsst_dictionary&) = default;

    /**
     * @brief Move assignment operator.
     * @details Transfers ownership of the underlying compressed data.
     */
    fsst_dictionary& operator=(fsst_dictionary&&) noexcept = default;

    /**
     * @brief Destructor.
     */
    ~fsst_dictionary();

    /**
     * @brief Retrieves and decompresses a string from the dictionary.
     *
     * @param key The key corresponding to the desired string.
     * @return std::optional<std::string> The decompressed string if the key
     * is valid (i.e., within the bounds of the dictionary data); otherwise
     * std::nullopt.
     *
     * @note This operation performs decompression on the fly. While highly
     * optimized, it involves memory allocation for the result string.
     */
    [[nodiscard]] std::optional<std::string> operator[](fsst_key key) const;

    /**
     * @brief Checks if the dictionary is empty.
     * @return true if the dictionary contains no data, false otherwise.
     */
    [[nodiscard]] bool empty() const;

    /**
     * @brief Returns the size of the internal compressed data blob in bytes.
     * @return The size in bytes.
     */
    [[nodiscard]] std::size_t size_in_bytes() const;

    /**
     * @brief Builds a compressed dictionary from a span of strings.
     *
     * @details
     * This factory method performs the following steps:
     * 1. Deduplicates the input strings to create a minimal training set.
     * 2. Trains an FSST encoder on the unique strings.
     * 3. Compresses all unique strings into a contiguous blob.
     * 4. Invokes the provided callback for every input string (in order),
     * passing the fsst_key that maps to its compressed representation.
     *
     * @param inputs The input strings to compress.
     * @param emit_key A callback function invoked once for each string in
     * `inputs`.
     * @return The resulting compressed fsst_dictionary.
     * @throws std::length_error If a string exceeds 16 MB or the total
     * size exceeds 1 TB.
     * @throws std::runtime_error If the FSST encoder cannot be created.
     */
    [[nodiscard]] static fsst_dictionary build(
      std::span<std::string const>  inputs,
      std::function<void(fsst_key)> emit_key);

    /**
     * @brief Builds a compressed dictionary and returns all keys.
     *
     * @details
     * A convenience overload that collects all keys into a vector instead
     * of using a callback.
     *
     * @param inputs The input strings to compress.
     * @return A pair containing:
     * - first: The compressed fsst_dictionary.
     * - second: A vector of fsst_key corresponding 1-to-1 with `inputs`.
     */
    [[nodiscard]] static std::pair<fsst_dictionary, std::vector<fsst_key>>
    build(std::span<std::string const> inputs);

  private:
    class impl;
    std::shared_ptr<impl const> p_impl;

    [[nodiscard]] explicit fsst_dictionary(
      std::shared_ptr<impl const> implementation);
  };

  // --- Generic Template Interface ---

  /**
   * @brief Compresses a range of objects into an FSST dictionary.
   *
   * @details
   * This utility function converts a range of arbitrary objects into strings
   * (via the provided projection), compresses them into a dictionary, and
   * writes the resulting keys to an output iterator.
   *
   * @tparam R The type of the input range.
   * @tparam Out The type of the output iterator (must accept fsst_key).
   * @tparam Proj The type of the projection function (defaults to identity).
   *
   * @param strings The input range to compress.
   * @param out The output iterator where keys will be written.
   * @param proj A projection function to transform elements of `strings`
   * into `std::string`.
   * @return fsst_dictionary The compressed dictionary containing the unique
   * data.
   */
  template <std::ranges::range R, typename Out, typename Proj = std::identity>
  [[nodiscard]] auto make_fsst_dictionary(R&& strings, Out out, Proj proj = {})
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

#endif // VAULT_ALGORITHM_FSST_DICTIONARY_HPP
