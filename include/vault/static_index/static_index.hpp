#ifndef VAULT_STATIC_INDEX_STATIC_INDEX_HPP
#define VAULT_STATIC_INDEX_STATIC_INDEX_HPP

#include <concepts>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <function2/function2.hpp>
#include <vault/static_index/traits.hpp>

namespace vault::containers {

  /**
   * @brief A 128-bit hash key used for indexing.
   */
  struct key_128 {
    /// The lower 64 bits of the 128-bit hash.
    uint64_t low;
    /// The higher 64 bits of the 128-bit hash.
    uint64_t high;

    /**
     * @brief Checks if two keys are identical.
     *
     * @param other The key to compare against.
     * @return true if both the low and high components match, false otherwise.
     */
    bool operator==(const key_128& other) const = default;
  };

  class static_index_builder;

  /**
   * @brief A read-only, static index for checking set membership or
   * retrieving associated values.
   *
   * The static_index is optimized for read-heavy workloads where the
   * dataset is immutable after construction. It uses a perfect
   * hashing scheme or similar static structure to ensure O(1)
   * lookups.
   *
   * The implementation details are hidden behind a Pimpl (Pointer to
   * Implementation) interface to ensure ABI stability and reduce
   * compile-time dependencies.
   *
   * @note This class is thread-safe for const access.
   */
  class static_index {
    friend class static_index_builder;

  public:
    /**
     * @brief Default constructor.
     *
     * Creates an empty index.
     */
    [[nodiscard]] static_index() = default;

    /**
     * @brief Copy constructor.
     *
     * Creates a new index sharing the underlying immutable implementation.
     * This is a cheap operation (pointer copy).
     */
    [[nodiscard]] static_index(const static_index&) = default;

    /**
     * @brief Move constructor.
     *
     * Transfers ownership of the underlying implementation.
     */
    [[nodiscard]] static_index(static_index&&) noexcept = default;

    /**
     * @brief Copy assignment operator.
     */
    static_index& operator=(const static_index&) = default;

    /**
     * @brief Move assignment operator.
     */
    static_index& operator=(static_index&&) noexcept = default;

    /**
     * @brief Destructor.
     */
    ~static_index();

    // --- Generalized Lookup ---

    /**
     * @brief Looks up an item in the index.
     *
     * Hashes the provided item and checks for its existence in the
     * index.
     *
     * @tparam T The type of the item to lookup. Must satisfy the
     * concepts::underlying_byte_sequences constraint.
     *
     * @param item The item to search for.
     *
     * @return std::optional<size_t> containing the index of the item
     * if found, or std::nullopt if the item is not present.
     */
    template <typename T>
      requires concepts::underlying_byte_sequences<std::remove_cvref_t<T>>
    [[nodiscard]] std::optional<size_t> operator[](T&& item) const noexcept
    {
      return lookup_impl([&](concepts::byte_sequence_visitor auto visitor) {
        traits::underlying_byte_sequences<std::remove_cvref_t<T>>::visit(
          std::forward<T>(item), visitor);
      });
    }

    /**
     * @brief Returns the estimated memory usage of the index in bytes.
     *
     * @return The size of the internal data structures in bytes.
     */
    [[nodiscard]] size_t memory_usage_bytes() const noexcept;

    /**
     * @brief Checks if the index is empty.
     *
     * @return true if the index contains no elements, false otherwise.
     */
    [[nodiscard]] bool empty() const noexcept;

  private:
    struct impl;
    /**
     * @brief Pointer to the implementation.
     *
     * Uses shared_ptr to allow cheap copying of the read-only index.
     */
    std::shared_ptr<const impl> pimpl_;

    /**
     * @brief Private constructor used by the Builder.
     *
     * @param ptr Shared pointer to the constructed implementation.
     */
    explicit static_index(std::shared_ptr<const impl> ptr);

    using bytes_sequence_sink =
      fu2::function_view<void(std::span<std::byte const>)>;

    [[nodiscard]] std::optional<std::size_t> lookup_impl(
      fu2::function_view<void(bytes_sequence_sink)> visitor) const;

    // --- Internal Hashing Helpers (Shared with Builder) ---
  };

  /**
   * @brief Builder class for constructing a static_index.
   *
   * Accumulates items, hashes them, and constructs an immutable static_index.
   * Uses "Deducing This" pattern to support method chaining for both lvalues
   * and rvalues.
   */
  struct static_index_builder {

    /**
     * @brief Adds a range of items to the builder.
     *
     * @tparam Self The type of the builder (deduced).
     * @tparam R The type of the range.
     * @param self The builder instance.
     * @param items The range of items to add.
     * @return Self&& The updated builder instance.
     */
    template <typename Self, std::ranges::input_range R>
      requires concepts::underlying_byte_sequences<
        std::remove_cvref_t<std::ranges::range_reference_t<R>>>
    Self add_n(this Self&& self, R&& items)
    {
      for (auto&& item : items) {
        self.add_1(std::forward<decltype(item)>(item));
      }

      return std::forward<Self>(self);
    }

    /**
     * @brief Adds a single item to the builder.
     *
     * Hashes the item immediately and stores the hash key.
     *
     * @tparam Self The type of the builder (deduced).
     * @tparam T The type of the item.
     * @param self The builder instance.
     * @param item The item to add.
     * @return Self&& The updated builder instance.
     */
    template <typename Self, typename T>
      requires concepts::underlying_byte_sequences<std::remove_cvref_t<T>>
    Self add_1(this Self&& self, T&& item)
    {
      self.add_1_impl([&](concepts::byte_sequence_visitor auto visitor) {
        traits::underlying_byte_sequences<std::remove_cvref_t<T>>::visit(
          std::forward<T>(item), visitor);
      });

      return std::forward<Self>(self);
    }

    [[nodiscard]] static_index build() &&;

    template <std::invocable<std::size_t> Sink>
    [[nodiscard]] std::pair<static_index, Sink> build(Sink sink) &&
    {
      auto index = std::move(*this).build_impl(std::ref(sink));
      return {std::move(index), std::move(sink)};
    }

    template <std::output_iterator<std::size_t> O>
    [[nodiscard]] std::pair<static_index, O> build(O out) &&
    {
      auto index =
        std::move(*this).build_impl([&](std::size_t idx) { *out++ = idx; });

      return {std::move(index), out};
    }

  private:
    using bytes_sequence_sink =
      fu2::function_view<void(std::span<std::byte const>)>;

    void add_1_impl(fu2::function_view<void(bytes_sequence_sink)>);

    [[nodiscard]] static_index build_impl(
      fu2::function_view<void(std::size_t)>) &&;

    std::vector<key_128> hash_cache_;
  };

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
