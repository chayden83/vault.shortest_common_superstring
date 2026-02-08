#ifndef VAULT_STATIC_INDEX_STATIC_INDEX_HPP
#define VAULT_STATIC_INDEX_STATIC_INDEX_HPP

#include <sys/mman.h>
#include <unistd.h>
#include <xxhash.h>

#include <concepts>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <vector>

#include <vault/pthash/pthash.hpp>

namespace vault::containers {

  template <typename T>
  concept string_like = requires(T t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<size_t>;
  };

  struct key_128 {
    uint64_t low;
    uint64_t high;

    bool operator==(const key_128& other) const
    {
      return low == other.low && high == other.high;
    }
  };

  struct hasher_128 {
    typedef pthash::hash128 hash_type;

    static inline hash_type hash(const key_128& key, uint64_t seed)
    {
      constexpr uint64_t c  = 0x9e3779b97f4a7c15ULL;
      uint64_t           h1 = (key.low ^ seed) * c;
      uint64_t           h2 = (key.high ^ seed) * c;
      h1 ^= (h1 >> 32);
      h2 ^= (h2 >> 32);
      return {h1, h2};
    }
  };

  template <typename T>
  [[nodiscard]] std::shared_ptr<T[]> make_huge_page_buffer(size_t count)
  {
    size_t bytes = count * sizeof(T);
    void*  ptr   = mmap(nullptr,
      bytes,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE,
      -1,
      0);

    if (ptr == MAP_FAILED) {
      ptr = mmap(nullptr,
        bytes,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
      if (ptr == MAP_FAILED) {
        throw std::bad_alloc();
      }
    }

    auto deleter = [bytes](T* p) {
      if (p) {
        munmap(p, bytes);
      }
    };

    return std::shared_ptr<T[]>(static_cast<T*>(ptr), deleter);
  }

  class static_index {
  public:
    using fingerprint_t = uint64_t;

    template <string_like key_type>
    void build(const std::vector<key_type>& keys)
    {
      // FIX: Handle empty input to prevent PTHash assertion failure
      if (keys.empty()) {
        num_fingerprints_ = 0;
        fingerprint_storage_.reset();
        return;
      }

      size_t               num_keys = keys.size();
      std::vector<key_128> hash_cache;
      hash_cache.reserve(num_keys);

      for (const auto& key : keys) {
        XXH128_hash_t h = XXH3_128bits(key.data(), key.size());
        hash_cache.push_back({h.low64, h.high64});
      }

      pthash::build_configuration config;
      config.alpha   = 0.94;
      config.lambda  = 3.5;
      config.verbose = false;

      mph_function_.build_in_internal_memory(
        hash_cache.begin(), hash_cache.size(), config);

      fingerprint_storage_    = make_huge_page_buffer<fingerprint_t>(num_keys);
      num_fingerprints_       = num_keys;
      fingerprint_t* raw_data = fingerprint_storage_.get();

      for (const auto& h : hash_cache) {
        uint64_t slot  = mph_function_(h);
        raw_data[slot] = h.high;
      }
    }

    [[nodiscard]] std::optional<size_t> lookup(
      std::string_view key) const noexcept
    {
      if (!fingerprint_storage_) [[unlikely]] {
        return std::nullopt;
      }

      XXH128_hash_t result = XXH3_128bits(key.data(), key.size());
      key_128       h      = {result.low64, result.high64};

      uint64_t slot = mph_function_(h);

      if (fingerprint_storage_[slot] == h.high) {
        return slot;
      }

      return std::nullopt;
    }

    [[nodiscard]] size_t memory_usage_bytes() const noexcept
    {
      return (mph_function_.num_bits() / 8)
        + (num_fingerprints_ * sizeof(fingerprint_t));
    }

  private:
    pthash::single_phf<hasher_128,
      pthash::skew_bucketer,
      pthash::dictionary_dictionary,
      true>
      mph_function_;

    std::shared_ptr<fingerprint_t[]> fingerprint_storage_;
    size_t                           num_fingerprints_ = 0;
  };

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
