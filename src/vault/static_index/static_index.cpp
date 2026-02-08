#include <cstring>
#include <new>
#include <sys/mman.h>
#include <vault/pthash/pthash.hpp>
#include <vault/static_index/static_index.hpp>

namespace vault::containers {

  // --- Internal Helpers ---

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

  // --- The Implementation Struct ---

  struct static_index::impl {
    using fingerprint_t = uint64_t;

    // 1. The MPH Function (Heavyweight object)
    pthash::single_phf<hasher_128,
      pthash::skew_bucketer,
      pthash::dictionary_dictionary,
      true>
      mph_function;

    // 2. Metadata
    size_t num_fingerprints = 0;

    // 3. The "Flexible Array Member"
    // We declare an array of size 1 to satisfy the compiler, but we never
    // access it directly without bounds checking logic.
    // In C++ standard, this is technically UB if accessed past [0], but
    // nearly all compilers support this pattern or we access via pointer math.
    fingerprint_t fingerprints[1];

    // Helper to get the pointer to the start of the array safely
    [[nodiscard]] const fingerprint_t* data() const { return &fingerprints[0]; }

    [[nodiscard]] fingerprint_t* data_mutable() { return &fingerprints[0]; }

    // --- Logic ---

    [[nodiscard]] std::optional<size_t> lookup(key_128 h) const
    {
      // Note: mph_function(h) is guaranteed to return [0, num_fingerprints - 1]
      // if the key exists.
      uint64_t slot = mph_function(h);

      // ACCESS: Direct offset from 'this' pointer. No second pointer
      // dereference.
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

  // --- Allocation & Construction Logic ---

  // Custom Deleter for the shared_ptr
  struct ImplDeleter {
    size_t total_size_bytes;

    void operator()(auto* ptr) const
    {
      if (ptr) {
        // 1. Manually call destructor for the C++ object
        ptr->~impl();
        // 2. Free the raw memory
        munmap(ptr, total_size_bytes);
      }
    }
  };

  static_index::static_index()  = default; // pimpl_ is null
  static_index::~static_index() = default;

  void static_index::clear() { pimpl_.reset(); }

  void static_index::build_internal(const std::vector<key_128>& hashes)
  {
    // 1. Configure and Build the PTHash function separately first
    //    We need to build it temporarily to know its size and state before we
    //    can copy it into the final destination. (Ideally PTHash would let us
    //    build into a buffer, but it owns its memory).

    auto temp_mph = pthash::single_phf<hasher_128,
      pthash::skew_bucketer,
      pthash::dictionary_dictionary,
      true>();

    {
      pthash::build_configuration config;
      config.alpha   = 0.94;
      config.lambda  = 3.5;
      config.verbose = false;

      // Mutable copy for permutation
      std::vector<key_128> mutable_hashes = hashes;
      temp_mph.build_in_internal_memory(
        mutable_hashes.begin(), mutable_hashes.size(), config);
    }

    // 2. Calculate Total Memory Needed
    //    Size = sizeof(impl) + (N-1 * sizeof(uint64_t))
    //    (N-1 because impl already includes space for fingerprints[0])
    size_t num_keys           = hashes.size();
    size_t extra_fingerprints = (num_keys > 0) ? (num_keys - 1) : 0;
    size_t total_bytes = sizeof(impl) + (extra_fingerprints * sizeof(uint64_t));

    // 3. Allocate Huge Pages
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

    // 4. Construct 'impl' in place
    //    We use placement new to initialize the C++ parts (vtables, pthash
    //    members)
    impl* impl_ptr = new (ptr) impl();

    // 5. Move the built PTHash function into the allocated block
    impl_ptr->mph_function     = std::move(temp_mph);
    impl_ptr->num_fingerprints = num_keys;

    // 6. Populate Fingerprints
    //    (Now we write directly to the tail of the allocation)
    uint64_t* raw_data = impl_ptr->data_mutable();
    for (const auto& h : hashes) {
      uint64_t slot  = impl_ptr->mph_function(h);
      raw_data[slot] = h.high;
    }

    // 7. Store in shared_ptr with custom deleter
    pimpl_ = std::shared_ptr<const impl>(impl_ptr, ImplDeleter{total_bytes});
  }

  [[nodiscard]] std::optional<size_t> static_index::lookup_internal(
    key_128 h) const noexcept
  {
    if (!pimpl_) [[unlikely]] {
      return std::nullopt;
    }

    return pimpl_->lookup(h);
  }

  [[nodiscard]] size_t static_index::memory_usage_bytes() const noexcept
  {
    return pimpl_ ? pimpl_->memory_usage() : 0;
  }

  XXH3_state_t* static_index::get_thread_local_state()
  {
    static thread_local XXH3_state_t* state = XXH3_createState();
    return state;
  }

} // namespace vault::containers
