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
      std::vector<key_128> hash_cache;
      hash_cache.reserve(items.size());

      // Prepare state once (thread local to save allocation)
      XXH3_state_t* state = get_thread_local_state();

      for (const auto& item : items) {
        hash_cache.push_back(hash_object(item, state));
      }

      build_internal(hash_cache);
    }

    // --- Generalized Lookup ---
    template <concepts::underlying_byte_sequences T>
    [[nodiscard]] std::optional<size_t> lookup(const T& item) const noexcept
    {
      // 1. Hash locally (Fully inlined)
      key_128 key = hash_object(item, get_thread_local_state());

      // 2. Pass 128-bit key to opaque impl
      return lookup_internal(key);
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

    // Helper to manage thread-local XXHash state
    static XXH3_state_t* get_thread_local_state()
    {
      static thread_local XXH3_state_t* state = XXH3_createState();
      return state;
    }

    // The hashing logic
    template <typename T>
    static key_128 hash_object(const T& item, XXH3_state_t* state)
    {
      XXH3_128bits_reset(state);

      // The generic visitor lambda matching concepts::byte_sequence_visitor
      auto visitor = [state](std::span<std::byte const> bytes) {
        XXH3_128bits_update(state, bytes.data(), bytes.size_bytes());
      };

      // Compile-time dispatch via traits namespace
      traits::underlying_byte_sequences<T>::visit(item, visitor);

      XXH128_hash_t res = XXH3_128bits_digest(state);
      return {res.low64, res.high64};
    }
  };

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
