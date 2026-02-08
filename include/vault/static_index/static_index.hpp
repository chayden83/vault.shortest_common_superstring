#ifndef VAULT_STATIC_INDEX_STATIC_INDEX_HPP
#define VAULT_STATIC_INDEX_STATIC_INDEX_HPP

#include <memory>
#include <optional>
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
      return {hash.high64, hash.high64};
    }

    bool operator==(const key_128& other) const = default;
  };

  class static_index {
  public:
    static_index();
    ~static_index();

    // --- Generalized Build ---
    // Constraints: T must satisfy the underlying_byte_sequences concept
    template <concepts::underlying_byte_sequences T>
    void build(const std::vector<T>& items)
    {
      if (items.empty()) {
        clear();
        return;
      }
      auto hash_cache = std::vector<key_128>{};
      hash_cache.reserve(items.size());

      auto* state = get_thread_local_state();

      for (const auto& item : items) {
        hash_cache.push_back(hash_object(item, state));
      }

      build_internal(hash_cache);
    }

    // --- Generalized Lookup ---
    template <concepts::underlying_byte_sequences T>
    [[nodiscard]] std::optional<size_t> lookup(const T& item) const noexcept
    {
      return lookup_internal(hash_object(item, get_thread_local_state()));
    }

    [[nodiscard]] size_t memory_usage_bytes() const noexcept;
    void                 clear();

  private:
    struct impl;
    std::shared_ptr<const impl> pimpl_;

    // Internal helpers
    void build_internal(const std::vector<key_128>& hashes);

    [[nodiscard]] std::optional<size_t> lookup_internal(
      key_128 key) const noexcept;

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

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
