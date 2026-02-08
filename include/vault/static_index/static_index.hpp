#ifndef VAULT_STATIC_INDEX_STATIC_INDEX_HPP
#define VAULT_STATIC_INDEX_STATIC_INDEX_HPP

#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <xxhash.h>

#include <vault/static_index/traits.hpp>

namespace vault::containers {

  struct key_128 {
    uint64_t low;
    uint64_t high;

    [[nodiscard]] static constexpr key_128 from_xxhash(
      XXH128_hash_t const& hash)
    {
      return {hash.low64, hash.high64};
    }

    bool operator==(const key_128& other) const = default;
  };

  class static_index_builder;

  class static_index {
    friend class static_index_builder;

  public:
    [[nodiscard]] static_index() = default;

    [[nodiscard]] static_index(static_index&&) noexcept = default;
    [[nodiscard]] static_index(const static_index&)     = default;

    static_index& operator=(const static_index&)     = default;
    static_index& operator=(static_index&&) noexcept = default;

    ~static_index();

    // --- Generalized Lookup ---
    template <concepts::underlying_byte_sequences T>
    [[nodiscard]] std::optional<size_t> lookup(const T& item) const noexcept
    {
      return lookup_internal(hash_object(item, get_thread_local_state()));
    }

    [[nodiscard]] size_t memory_usage_bytes() const noexcept;
    [[nodiscard]] bool   empty() const noexcept;

  private:
    struct impl;
    std::shared_ptr<const impl> pimpl_;

    // Private constructor used by the Builder
    explicit static_index(std::shared_ptr<const impl> ptr);

    [[nodiscard]] std::optional<size_t> lookup_internal(
      key_128 key) const noexcept;

    // --- Internal Hashing Helpers (Shared with Builder) ---
    [[nodiscard]] static XXH3_state_t* get_thread_local_state();

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

  struct static_index_builder {
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

    template <typename Self, typename T>
      requires concepts::underlying_byte_sequences<std::remove_cvref_t<T>>
    Self add_1(this Self&& self, T&& item)
    {
      self.hash_cache_.push_back(static_index::hash_object(
        item, static_index::get_thread_local_state()));

      return std::forward<Self>(self);
    }

    [[nodiscard]] static_index build() &&;

  private:
    std::vector<key_128> hash_cache_;
  };

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
