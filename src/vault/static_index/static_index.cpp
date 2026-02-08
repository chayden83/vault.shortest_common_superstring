#include <cstring>
#include <new>

#include <sys/mman.h>

#include <vault/pthash/pthash.hpp>
#include <vault/pthash/utils/hasher.hpp>

#include <vault/static_index/static_index.hpp>

namespace vault::containers {

  // --- Internal Helpers ---

  struct hasher_128 {
    using hash_type = pthash::hash128;

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

  // --- The Implementation Struct ---

  struct static_index::impl {
    using fingerprint_t = uint64_t;

    pthash::single_phf<hasher_128,
      pthash::skew_bucketer,
      pthash::dictionary_dictionary,
      true>
      mph_function;

    size_t num_fingerprints = 0;

    // Flexible Array Member for fingerprints
    fingerprint_t fingerprints[1];

    [[nodiscard]] const fingerprint_t* data() const { return &fingerprints[0]; }

    [[nodiscard]] fingerprint_t* data_mutable() { return &fingerprints[0]; }

    [[nodiscard]] std::optional<size_t> lookup(key_128 h) const
    {
      uint64_t slot = mph_function(h);
      if (data()[slot] == h.high) {
        return slot;
      }
      return std::nullopt;
    }

    [[nodiscard]] size_t memory_usage() const
    {
      return (mph_function.num_bits() / 8)
        + (num_fingerprints * sizeof(fingerprint_t));
    }
  };

  // --- Custom Deleter ---

  struct ImplDeleter {
    size_t total_size_bytes;

    void operator()(auto* ptr) const
    {
      if (ptr) {
        ptr->~impl();
        munmap(ptr, total_size_bytes);
      }
    }
  };

  // --- static_index Implementation ---

  static_index::~static_index() = default;

  // Private constructor called by builder
  static_index::static_index(std::shared_ptr<const impl> ptr)
      : pimpl_(std::move(ptr))
  {}

  std::optional<size_t> static_index::lookup_internal(key_128 h) const noexcept
  {
    if (!pimpl_) [[unlikely]] {
      return std::nullopt;
    }
    return pimpl_->lookup(h);
  }

  size_t static_index::memory_usage_bytes() const noexcept
  {
    return pimpl_ ? pimpl_->memory_usage() : 0;
  }

  bool static_index::empty() const noexcept { return !pimpl_; }

  XXH3_state_t* static_index::get_thread_local_state()
  {
    static thread_local XXH3_state_t* state = XXH3_createState();
    return state;
  }

  // --- static_index_builder Implementation ---

  static_index static_index_builder::build() &&
  {
    if (hash_cache_.size() == 0) {
      return {};
    }

    // 1. Build PTHash structure temporarily
    auto temp_mph = pthash::single_phf<hasher_128,
      pthash::skew_bucketer,
      pthash::dictionary_dictionary,
      true>();

    {
      pthash::build_configuration config;
      config.alpha   = 0.94;
      config.lambda  = 3.5;
      config.verbose = false;

      temp_mph.build_in_internal_memory(
        hash_cache_.begin(), hash_cache_.size(), config);
    }

    // 2. Allocate Memory
    size_t num_keys           = hash_cache_.size();
    size_t extra_fingerprints = (num_keys > 0) ? (num_keys - 1) : 0;
    size_t total_bytes =
      sizeof(static_index::impl) + (extra_fingerprints * sizeof(uint64_t));

    void* ptr = mmap(nullptr,
      total_bytes,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE,
      -1,
      0);

    if (ptr == MAP_FAILED) {
      ptr = mmap(nullptr,
        total_bytes,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
      if (ptr == MAP_FAILED) {
        throw std::bad_alloc();
      }
    }

    // 3. Placement New & Move
    auto* impl_ptr             = new (ptr) static_index::impl();
    impl_ptr->mph_function     = std::move(temp_mph);
    impl_ptr->num_fingerprints = num_keys;

    // 4. Write Fingerprints
    uint64_t* raw_data = impl_ptr->data_mutable();
    for (const auto& h : hash_cache_) {
      uint64_t slot  = impl_ptr->mph_function(h);
      raw_data[slot] = h.high;
    }

    // 5. Return wrapped in static_index
    return static_index(std::shared_ptr<const static_index::impl>(
      impl_ptr, ImplDeleter{total_bytes}));
  }

} // namespace vault::containers
