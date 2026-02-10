#include <function2/function2.hpp>
#include <ranges>
#include <sys/mman.h>

#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include <vault/pthash/pthash.hpp>
#include <vault/pthash/utils/hasher.hpp>

#include <vault/static_index/static_index.hpp>

namespace vault::containers {
  namespace {

    // --- Internal Helpers ---

    struct hasher_128 {
      using hash_type = pthash::hash128;

      static inline hash_type hash(const key_128& key, uint64_t seed)
      {
        constexpr uint64_t c = 0x9e3779b97f4a7c15ULL;

        uint64_t h1 = (key.low ^ seed) * c;
        uint64_t h2 = (key.high ^ seed) * c;

        h1 ^= (h1 >> 32);
        h2 ^= (h2 >> 32);

        return {h1, h2};
      }
    };

    // --- Custom Deleter ---

    struct impl_deleter {
      size_t total_size_bytes;

      template <typename T> void operator()(T const* ptr) const
      {
        if (ptr) {
          std::destroy_at(const_cast<T*>(ptr));
          munmap(const_cast<T*>(ptr), total_size_bytes);
        }
      }
    };
  } // namespace

  // --- The Implementation Struct ---

  struct static_index::impl {
    using fingerprint_t = uint64_t;

    pthash::single_phf<hasher_128,
      pthash::skew_bucketer,
      pthash::dictionary_dictionary,
      true>
      mph_function;

    size_t        num_fingerprints = 0;
    fingerprint_t fingerprints[];

    [[nodiscard]] std::optional<size_t> lookup(key_128 h) const
    {
      uint64_t slot = mph_function(h);
      assert(slot < num_fingerprints);

      if (fingerprints[slot] == h.high) {
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

  // --- static_index Implementation ---

  static_index::~static_index() = default;

  // Private constructor called by builder
  static_index::static_index(std::shared_ptr<const impl> ptr)
      : pimpl_(std::move(ptr))
  {}

  std::optional<size_t> static_index::lookup_impl(
    fu2::function_view<void(bytes_sequence_sink)> visitor) const
  {
    if (!pimpl_) [[unlikely]] {
      return std::nullopt;
    }

    auto* state = static_index::get_thread_local_state();

    XXH3_128bits_reset(state);

    visitor([=](std::span<std::byte const> bytes) {
      XXH3_128bits_update(state, bytes.data(), bytes.size_bytes());
    });

    return pimpl_->lookup(key_128::from_xxhash(XXH3_128bits_digest(state)));
  }

  size_t static_index::memory_usage_bytes() const noexcept
  {
    return pimpl_ ? pimpl_->memory_usage() : 0;
  }

  bool static_index::empty() const noexcept { return !pimpl_; }

  XXH3_state_t* static_index::get_thread_local_state()
  {
    using state_ptr = std::unique_ptr<XXH3_state_t,
      XXH_NAMESPACEXXH_errorcode (*)(XXH3_state_t*)>;

    static thread_local state_ptr state{XXH3_createState(), &XXH3_freeState};
    return state.get();
  }

  // --- static_index_builder Implementation ---

  static_index static_index_builder::build() &&
  {
    return std::move(*this).build_impl([](auto&&...) {});
  }

  void static_index_builder::add_1_impl(
    fu2::function_view<void(bytes_sequence_sink)> visitor)
  {
    auto* state = static_index::get_thread_local_state();

    XXH3_128bits_reset(state);

    visitor([=](std::span<std::byte const> bytes) {
      XXH3_128bits_update(state, bytes.data(), bytes.size_bytes());
    });

    hash_cache_.emplace_back(key_128::from_xxhash(XXH3_128bits_digest(state)));
  }

  static_index static_index_builder::build_impl(
    fu2::function_view<void(std::size_t)> sink) &&
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
    size_t total_bytes =
      sizeof(static_index::impl) + (hash_cache_.size() * sizeof(uint64_t));

    auto* ptr = mmap(nullptr,
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
    }

    if (ptr == MAP_FAILED) {
      throw std::bad_alloc();
    }

    try {
      auto* impl_ptr = new (ptr) static_index::impl();

      try {
        impl_ptr->mph_function     = std::move(temp_mph);
        impl_ptr->num_fingerprints = hash_cache_.size();

        uint64_t* raw_data = impl_ptr->fingerprints;

        for (auto const& h : hash_cache_) {
          auto slot      = impl_ptr->mph_function(h);
          raw_data[slot] = h.high;
          sink(slot);
        }

        return static_index(std::shared_ptr<const static_index::impl>(
          impl_ptr, impl_deleter{total_bytes}));
      } catch (...) {
        std::destroy_at(impl_ptr);
        throw;
      }
    } catch (...) {
      munmap(ptr, total_bytes);
      throw;
    }
  }

} // namespace vault::containers
