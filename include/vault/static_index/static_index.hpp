#ifndef VAULT_STATIC_INDEX_STATIC_INDEX_HPP
#define VAULT_STATIC_INDEX_STATIC_INDEX_HPP

#include <concepts>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include <xxhash.h>

namespace vault::containers {

  struct key_128 {
    uint64_t low;
    uint64_t high;

    [[nodiscard]] bool operator==(const key_128& other) const = default;
  };

  template <typename T>
  concept string_like = requires(T t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<size_t>;
  };

  class static_index {
  public:
    [[nodiscard]] static_index();
    ~static_index();

    /**
     * @brief Builds the index from a collection of keys.
     * This operation is atomic: it builds a new index in the background
     * and swaps the pointer only when complete. Existing copies of this
     * index object are unaffected (they keep pointing to the old data).
     */
    template <string_like key_type>
    void build(const std::vector<key_type>& keys)
    {
      if (keys.empty()) {
        clear();
        return;
      }

      std::vector<key_128> hash_cache;
      hash_cache.reserve(keys.size());

      for (const auto& key : keys) {
        XXH128_hash_t h = XXH3_128bits(key.data(), key.size());
        hash_cache.push_back({h.low64, h.high64});
      }

      build_internal(hash_cache);
    }

    [[nodiscard]] std::optional<size_t> lookup(
      std::string_view key) const noexcept;
    [[nodiscard]] size_t memory_usage_bytes() const noexcept;

    // Resets the handle to an empty state
    void clear();

  private:
    void build_internal(const std::vector<key_128>& hashes);

    struct impl;
    std::shared_ptr<const impl> pimpl_;
  };

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
