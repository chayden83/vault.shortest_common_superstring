#include <cstdlib>
#include <xxhash.h>

#include <sys/mman.h>

#include <cstring>
#include <memory>
#include <new>
#include <utility>

#include <function2/function2.hpp>

#include <vault/pthash/pthash.hpp>
#include <vault/pthash/utils/hasher.hpp>

#include <vault/static_index/static_index.hpp>

namespace vault::containers {
  namespace {

    // --- Internal Helpers ---

    struct hasher_128 {
      using hash_type = pthash::hash128;

      static inline hash_type hash(const key_128& key, uint64_t seed) {
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
      template <typename T>
      void operator()(T const* ptr) const {
        if (ptr) {
          std::destroy_at(const_cast<T*>(ptr));
          free(const_cast<T*>(ptr));
        }
      }
    };

    [[nodiscard]] constexpr key_128 key_128_from_xxhash(XXH128_hash_t const& hash) {
      return {hash.low64, hash.high64};
    }

    XXH3_state_t* get_thread_local_state() {
      using state_ptr = std::unique_ptr<XXH3_state_t, XXH_NAMESPACEXXH_errorcode (*)(XXH3_state_t*)>;

      static thread_local state_ptr state{XXH3_createState(), &XXH3_freeState};
      return state.get();
    }

  } // namespace

  // --- The Implementation Struct ---

  struct static_index_base::impl {
    pthash::single_phf<hasher_128, pthash::skew_bucketer, pthash::dictionary_dictionary, true> mph_function;

    [[nodiscard]] std::pair<size_t, key_128> lookup(key_128 h) const {
      return {mph_function(h), h};
    }

    [[nodiscard]] size_t memory_usage() const {
      return mph_function.num_bits() / 8;
    }
  };

  // --- static_index_base Implementation ---

  static_index_base::~static_index_base() = default;

  static_index_base::static_index_base(std::shared_ptr<const impl> ptr)
    : pimpl_(std::move(ptr)) {}

  std::pair<size_t, key_128> static_index_base::operator[](key_128 hash) const {
    if (!pimpl_) [[unlikely]] {
      return {npos, hash};
    } else {
      return pimpl_->lookup(hash);
    }
  }

  std::pair<size_t, key_128> static_index_base::operator[](bytes_sequence_channel_t visitor) const {
    return operator[](hash(visitor));
  }

  size_t static_index_base::memory_usage_bytes() const noexcept {
    return pimpl_ ? pimpl_->memory_usage() : 0;
  }

  bool static_index_base::empty() const noexcept {
    return !pimpl_;
  }

  key_128 static_index_base::hash(bytes_sequence_channel_t channel) {
    auto* state = get_thread_local_state();
    XXH3_128bits_reset(state);

    // clang-format off
    channel([=](std::span<std::byte const> bytes) {
      XXH3_128bits_update(state, bytes.data(), bytes.size_bytes());
    });
    // clang-format on

    return key_128_from_xxhash(XXH3_128bits_digest(state));
  }

  // --- static_index_builder Implementation ---

  static_index_base static_index_base::build(std::span<key_128 const> keys) {
    if (keys.empty()) {
      return {};
    }

    // 1. Build PTHash structure temporarily
    auto temp_mph = pthash::single_phf<hasher_128, pthash::skew_bucketer, pthash::dictionary_dictionary, true>();

    {
      pthash::build_configuration config;

      config.alpha   = 0.94;
      config.lambda  = 3.5;
      config.verbose = false;

      temp_mph.build_in_internal_memory(keys.begin(), keys.size(), config);
    }

    // 2. Allocate Memory
    size_t total_bytes = sizeof(static_index_base::impl);

    void* ptr = nullptr;

    if (total_bytes >= 2 * 1024 * 1024) {
      posix_memalign(&ptr, 2 * 1024 * 1024, total_bytes);

#if defined(MADV_COLLAPSE)
      ::madvise(ptr, total_bytes, MADV_COLLAPSE);
#elif defined(MADV_HUGEPAGE)
      ::madvise(ptr, total_bytes, MADV_HUGEPAGE);
#endif
    } else {
      ptr = malloc(total_bytes);
    }

    try {
      auto* impl_ptr = new (ptr) static_index_base::impl();

      try {
        impl_ptr->mph_function = std::move(temp_mph);
        return static_index_base(std::shared_ptr<const static_index_base::impl>(impl_ptr, impl_deleter{}));
      } catch (...) {
        std::destroy_at(impl_ptr);
        throw;
      }
    } catch (...) {
      free(ptr);
      throw;
    }
  }

} // namespace vault::containers

// --- Specialized Static Index for Integral Type Fingerprints ---

namespace vault::containers {
  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  struct specialized_static_index_base<Fingerprint>::impl {
    pthash::single_phf<hasher_128, pthash::skew_bucketer, pthash::dictionary_dictionary, true> mph_function;

    std::size_t nfingerprints = 0;
    Fingerprint fingerprints[];
  };

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  specialized_static_index_base<Fingerprint>::specialized_static_index_base(std::shared_ptr<impl const> pimpl)
    : pimpl_(std::move(pimpl)) {}

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  specialized_static_index_base<Fingerprint>::~specialized_static_index_base() = default;

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  bool specialized_static_index_base<Fingerprint>::empty() const noexcept {
    return pimpl_->nfingerprints == 0;
  }

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  size_t specialized_static_index_base<Fingerprint>::memory_usage_bytes() const noexcept {
    return (pimpl_->mph_function.num_bits() / 8) + sizeof(pimpl_->nfingerprints) +
           (pimpl_->nfingerprints * sizeof(Fingerprint));
  }

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  key_128 specialized_static_index_base<Fingerprint>::hash(bytes_sequence_channel_t channel) {
    return static_index_base::hash(channel);
  }

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  std::tuple<std::size_t, key_128, Fingerprint> specialized_static_index_base<Fingerprint>::operator[](key_128 hash
  ) const {
    if (!pimpl_) [[unlikely]] {
      return {npos, hash, {}};
    } else {
      auto slot = pimpl_->mph_function(hash);
      return {slot, hash, pimpl_->fingerprints[slot]};
    }
  }

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  std::tuple<std::size_t, key_128, Fingerprint>
  specialized_static_index_base<Fingerprint>::operator[](bytes_sequence_channel_t channel) const {
    return operator[](hash(channel));
  }

  template <typename Fingerprint>
    requires std::is_integral_v<Fingerprint>
  specialized_static_index_base<Fingerprint> specialized_static_index_base<Fingerprint>::build(
    std::span<key_128 const>     hashes,
    std::span<Fingerprint const> fingerprints
  ) {
    if (hashes.size() == 0) {
      return {};
    }

    // 1. Build PTHash structure temporarily
    auto temp_mph = pthash::single_phf<hasher_128, pthash::skew_bucketer, pthash::dictionary_dictionary, true>();

    {
      pthash::build_configuration config;

      config.alpha   = 0.94;
      config.lambda  = 3.5;
      config.verbose = false;

      temp_mph.build_in_internal_memory(hashes.begin(), hashes.size(), config);
    }

    // 2. Allocate Memory
    size_t total_bytes = sizeof(impl) + (hashes.size() * sizeof(Fingerprint));

    void* ptr = nullptr;

    if (total_bytes >= 2 * 1024 * 1024) {
      posix_memalign(&ptr, 2 * 1024 * 1024, total_bytes);

#if defined(MADV_COLLAPSE)
      ::madvise(ptr, total_bytes, MADV_COLLAPSE);
#elif defined(MADV_HUGEPAGE)
      ::madvise(ptr, total_bytes, MADV_HUGEPAGE);
#endif
    } else {
      ptr = malloc(total_bytes);
    }

    try {
      auto* impl_ptr = new (ptr) impl();

      try {
        impl_ptr->mph_function  = std::move(temp_mph);
        impl_ptr->nfingerprints = hashes.size();

        Fingerprint* raw_data = impl_ptr->fingerprints;

        for (auto const& h : hashes) {
          auto slot      = impl_ptr->mph_function(h);
          raw_data[slot] = static_cast<Fingerprint>(h.high);
        }

        return specialized_static_index_base(std::shared_ptr<const impl>(impl_ptr, impl_deleter{}));
      } catch (...) {
        std::destroy_at(impl_ptr);
        throw;
      }
    } catch (...) {
      free(ptr);
      throw;
    }
  }

  template class specialized_static_index_base<bool>;
  template class specialized_static_index_base<char>;
  template class specialized_static_index_base<signed char>;
  template class specialized_static_index_base<unsigned char>;
  template class specialized_static_index_base<short>;
  template class specialized_static_index_base<unsigned short>;
  template class specialized_static_index_base<int>;
  template class specialized_static_index_base<unsigned int>;
  template class specialized_static_index_base<long>;
  template class specialized_static_index_base<unsigned long>;
  template class specialized_static_index_base<long long>;
  template class specialized_static_index_base<unsigned long long>;

  template class specialized_static_index_base<wchar_t>;
  template class specialized_static_index_base<char8_t>;
  template class specialized_static_index_base<char16_t>;
  template class specialized_static_index_base<char32_t>;
} // namespace vault::containers
