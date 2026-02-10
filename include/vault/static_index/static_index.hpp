#ifndef VAULT_STATIC_INDEX_STATIC_INDEX_HPP
#define VAULT_STATIC_INDEX_STATIC_INDEX_HPP

#include <concepts>
#include <xxhash.h>

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
   *
   * This structure wraps the low and high 64-bit components of an XXH128 hash.
   * It provides equality comparison and a factory method for conversion from
   * the native xxHash type.
   */
  struct key_128 {
    /// The lower 64 bits of the 128-bit hash.
    uint64_t low;
    /// The higher 64 bits of the 128-bit hash.
    uint64_t high;

    /**
     * @brief Constructs a key_128 from an XXH128_hash_t struct.
     *
     * @param hash The native xxHash struct containing the 128-bit hash.
     * @return A new key_128 instance initialized with the hash's components.
     */
    [[nodiscard]] static constexpr key_128 from_xxhash(
      XXH128_hash_t const& hash)
    {
      return {hash.low64, hash.high64};
    }

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
     * Hashes the provided item using XXH3_128 and checks for its existence
     * in the index.
     *
     * @tparam T The type of the item to lookup. Must satisfy the
     * concepts::underlying_byte_sequences constraint.
     * @param item The item to search for.
     * @return std::optional<size_t> containing the index of the item if found,
     * or std::nullopt if the item is not present.
     */
    template <concepts::underlying_byte_sequences T>
    [[nodiscard]] std::optional<size_t> operator[](const T& item) const noexcept
    {
      return lookup_internal(hash_object(item, get_thread_local_state()));
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

    /**
     * @brief Internal lookup implementation using a pre-computed 128-bit key.
     *
     * @param key The 128-bit hash of the item.
     * @return std::optional<size_t> containing the index if found.
     */
    [[nodiscard]] std::optional<size_t> lookup_internal(
      key_128 key) const noexcept;

    // --- Internal Hashing Helpers (Shared with Builder) ---

    /**
     * @brief Retrieves the thread-local XXH3 state.
     *
     * @return Pointer to the thread-local XXH3_state_t.
     */
    [[nodiscard]] static XXH3_state_t* get_thread_local_state();

    /**
     * @brief Hashes an object using XXH3_128.
     *
     * @tparam T The type of the item to hash.
     * @param item The item to hash.
     * @param state The XXH3 state to use for hashing.
     * @return The 128-bit hash key.
     */
    template <typename T>
    [[nodiscard]] static key_128 hash_object(const T& item, XXH3_state_t* state)
    {
      XXH3_128bits_reset(state);

      traits::underlying_byte_sequences<T>::visit(
        item, [state](std::span<std::byte const> bytes) {
          XXH3_128bits_update(state, bytes.data(), bytes.size_bytes());
        });

      return key_128::from_xxhash(XXH3_128bits_digest(state));
    }
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
      self.hash_cache_.push_back(static_index::hash_object(
        item, static_index::get_thread_local_state()));

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
    [[nodiscard]] static_index build_impl(
      fu2::function_view<void(std::size_t)>) &&;

    /// Cache of hashed keys to be included in the index.
    std::vector<key_128> hash_cache_;
  };

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
