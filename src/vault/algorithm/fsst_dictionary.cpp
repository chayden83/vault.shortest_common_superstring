// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

#include <fsst.h>

namespace vault::algorithm {

  namespace {
    // --- Key Layout Constants ---
    constexpr std::size_t kInlineFlagShift = 63;
    constexpr std::size_t kInlineLenShift  = 56;
    constexpr std::size_t kInlineLenMask   = 0x7F; // 7 bits

    constexpr std::size_t kPointerLenShift   = 40;
    constexpr std::size_t kPointerLenMask    = 0x7F'FFFF;         // 23 bits
    constexpr std::size_t kPointerOffsetMask = 0xFF'FFFF'FFFFULL; // 40 bits

    constexpr std::size_t kByteMask = 0xFF;

    // Derived Limits
    constexpr std::size_t kMaxPointerLength = kPointerLenMask;
    constexpr std::size_t kMaxPointerOffset = kPointerOffsetMask;
    constexpr std::size_t kMaxInlineLength  = 7;

    // --- Internal Helpers ---

    auto create_pointer_key(std::size_t offset, std::size_t length) -> fsst_key
    {
      if (length > kMaxPointerLength) {
        throw std::length_error("fsst_key: string exceeds 8MB limit");
      }
      return fsst_key{(static_cast<std::uint64_t>(length) << kPointerLenShift)
        | (offset & kPointerOffsetMask)};
    }

    auto key_is_inline(fsst_key k) -> bool
    {
      return (k.value >> kInlineFlagShift) & 1;
    }

    auto extract_inline_string(fsst_key k) -> std::string
    {
      std::string s;
      std::size_t len = (k.value >> kInlineLenShift) & kInlineLenMask;
      s.resize(len);
      for (std::size_t i = 0; i < len; ++i) {
        s[i] = static_cast<char>((k.value >> (i * 8)) & kByteMask);
      }
      return s;
    }

    auto decode_pointer_key(fsst_key k) -> std::pair<std::size_t, std::size_t>
    {
      std::size_t len = (k.value >> kPointerLenShift) & kPointerLenMask;
      std::size_t off = k.value & kPointerOffsetMask;
      return {off, len};
    }

  } // namespace

  // --- Public Helper Implementations ---

  bool fsst_dictionary::is_inline_candidate(std::string_view s) noexcept
  {
    return s.size() <= kMaxInlineLength;
  }

  fsst_key fsst_dictionary::make_inline_key(std::string_view s)
  {
    if (s.size() > kMaxInlineLength) {
      throw std::length_error("fsst_key: inline string too long");
    }
    std::uint64_t payload = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
      payload |= (static_cast<std::uint64_t>(static_cast<unsigned char>(s[i]))
        << (i * 8));
    }
    payload |= (static_cast<std::uint64_t>(s.size()) << kInlineLenShift);
    payload |= (1ULL << kInlineFlagShift);
    return fsst_key{payload};
  }

  // --- Dictionary Implementation ---

  struct fsst_dictionary::impl {
    std::vector<unsigned char> data_blob;
    fsst_decoder_t             decoder;

    impl() { std::memset(&decoder, 0, sizeof(decoder)); }
  };

  fsst_dictionary::fsst_dictionary()
      : p_impl(std::make_shared<impl const>())
  {}

  fsst_dictionary::~fsst_dictionary() = default;

  fsst_dictionary::fsst_dictionary(std::shared_ptr<impl const> implementation)
      : p_impl(std::move(implementation))
  {}

  bool fsst_dictionary::empty() const
  {
    return !p_impl || p_impl->data_blob.empty();
  }

  std::size_t fsst_dictionary::size_in_bytes() const
  {
    return p_impl ? p_impl->data_blob.size() : 0;
  }

  std::optional<std::string> fsst_dictionary::operator[](fsst_key key) const
  {
    if (key_is_inline(key)) {
      return extract_inline_string(key);
    }
    if (empty()) {
      return std::nullopt;
    }

    auto [offset, length] = decode_pointer_key(key);
    if (offset + length > p_impl->data_blob.size()) {
      return std::nullopt;
    }

    auto const* compressed_ptr = p_impl->data_blob.data() + offset;
    auto        result         = std::string{};
    result.resize(std::max(std::size_t{64}, length * 5));

    auto actual_size =
      fsst_decompress(const_cast<fsst_decoder_t*>(&p_impl->decoder),
        length,
        const_cast<unsigned char*>(compressed_ptr),
        result.size(),
        reinterpret_cast<unsigned char*>(result.data()));

    result.resize(actual_size);
    return result;
  }

  // --- Core FSST Compression ---

  std::pair<std::shared_ptr<fsst_dictionary::impl>, std::vector<fsst_key>>
  fsst_dictionary::compress_core(std::size_t count,
    std::size_t*                             lens,
    unsigned char const**                    ptrs,
    float                                    sample_ratio)
  {
    if (sample_ratio <= 0.0f || sample_ratio > 1.0f) {
      throw std::invalid_argument("sample_ratio must be in (0, 1]");
    }
    if (count == 0) {
      return {std::make_shared<fsst_dictionary::impl>(), {}};
    }

    fsst_encoder_t* encoder = nullptr;
    std::size_t     target_samples =
      static_cast<std::size_t>(std::ceil(count * sample_ratio));
    target_samples = std::max(target_samples, std::size_t{1024});

    if (target_samples >= count) {
      encoder = fsst_create(count, lens, ptrs, 0);
    } else {
      auto indices = std::vector<std::size_t>(count);
      std::iota(indices.begin(), indices.end(), 0);

      auto sampled_indices = std::vector<std::size_t>{};
      sampled_indices.resize(target_samples);

      auto rng = std::mt19937{std::random_device{}()};
      std::sample(indices.begin(),
        indices.end(),
        sampled_indices.begin(),
        target_samples,
        rng);

      auto training_ptrs = std::vector<unsigned char const*>{};
      auto training_lens = std::vector<std::size_t>{};
      training_ptrs.reserve(target_samples);
      training_lens.reserve(target_samples);

      for (auto idx : sampled_indices) {
        training_ptrs.push_back(ptrs[idx]);
        training_lens.push_back(lens[idx]);
      }
      encoder = fsst_create(
        target_samples, training_lens.data(), training_ptrs.data(), 0);
    }

    if (!encoder) {
      throw std::runtime_error("Failed to create FSST encoder");
    }

    auto impl_ptr = std::make_shared<fsst_dictionary::impl>();

    alignas(8) unsigned char buf[FSST_MAXHEADER];
    fsst_export(encoder, buf);
    fsst_import(&impl_ptr->decoder, buf);

    std::size_t total_input_size = 0;
    std::size_t max_len          = 0;
    for (size_t i = 0; i < count; ++i) {
      total_input_size += lens[i];
      max_len = std::max(max_len, lens[i]);
    }

    impl_ptr->data_blob.reserve(total_input_size + 1024);

    auto keys = std::vector<fsst_key>{};
    keys.reserve(count);

    std::size_t                     buffer_req = 2 * max_len + 16;
    std::vector<unsigned long long> aligned_buf((buffer_req + 7) / 8);
    unsigned char*                  comp_buf_ptr =
      reinterpret_cast<unsigned char*>(aligned_buf.data());
    std::size_t comp_buf_len = aligned_buf.size() * 8;

    for (size_t i = 0; i < count; ++i) {
      if (impl_ptr->data_blob.size() >= kMaxPointerOffset) {
        fsst_destroy(encoder);
        throw std::length_error("fsst_dictionary: Dictionary size limit");
      }

      auto  src_len = lens[i];
      auto  src_ptr = ptrs[i];
      auto  dst_len = std::size_t{0};
      auto* dst_ptr = static_cast<unsigned char*>(nullptr);

      fsst_compress(encoder,
        1,
        &src_len,
        &src_ptr,
        comp_buf_len,
        comp_buf_ptr,
        &dst_len,
        &dst_ptr);

      if (dst_len > kMaxPointerLength) {
        fsst_destroy(encoder);
        throw std::length_error("fsst_dictionary: Compressed string limit");
      }

      auto offset = impl_ptr->data_blob.size();

      impl_ptr->data_blob.insert(
        impl_ptr->data_blob.end(), comp_buf_ptr, comp_buf_ptr + dst_len);

      keys.push_back(create_pointer_key(offset, dst_len));
    }

    impl_ptr->data_blob.shrink_to_fit();

    fsst_destroy(encoder);
    return {std::move(impl_ptr), std::move(keys)};
  }

  // --- Factories (Core Implementations) ---

  fsst_dictionary fsst_dictionary::build(Generator gen,
    Deduplicator                                   dedup,
    std::function<void(fsst_key)>                  emit_key,
    sample_ratio                                   ratio)
  {
    std::vector<std::uint64_t>        instructions;
    std::vector<unsigned char const*> compression_ptrs;
    std::vector<std::size_t>          compression_lens;

    while (true) {
      auto opt_sv = gen();
      if (!opt_sv) {
        break;
      }

      std::string_view sv = *opt_sv;

      if (is_inline_candidate(sv)) {
        fsst_key k = make_inline_key(sv);
        instructions.push_back(k.value);
      } else {
        // Deduplicate
        auto [idx, is_new] = dedup(sv);

        if (is_new) {
          if (sv.size() > kMaxPointerLength) {
            throw std::length_error("String too large");
          }
          // Generator contract: sv points to stable memory (Existing or Arena)
          compression_ptrs.push_back(
            reinterpret_cast<unsigned char const*>(sv.data()));
          compression_lens.push_back(sv.size());
        }

        instructions.push_back(idx);
      }
    }

    auto [impl_ptr, large_keys] = compress_core(compression_ptrs.size(),
      compression_lens.data(),
      compression_ptrs.data(),
      ratio.value);

    fsst_dictionary result_dict(std::move(impl_ptr));

    for (std::uint64_t val : instructions) {
      if (val & (1ULL << 63)) {
        emit_key(fsst_key{val});
      } else {
        emit_key(large_keys[static_cast<std::size_t>(val)]);
      }
    }

    return result_dict;
  }

  fsst_dictionary fsst_dictionary::build_from_unique(
    Generator gen, std::function<void(fsst_key)> emit_key, sample_ratio ratio)
  {
    std::vector<std::uint64_t>        instructions;
    std::vector<unsigned char const*> compression_ptrs;
    std::vector<std::size_t>          compression_lens;
    std::size_t                       current_ptr_idx = 0;

    while (true) {
      auto opt_sv = gen();
      if (!opt_sv) {
        break;
      }

      std::string_view sv = *opt_sv;

      if (is_inline_candidate(sv)) {
        fsst_key k = make_inline_key(sv);
        instructions.push_back(k.value);
      } else {
        if (sv.size() > kMaxPointerLength) {
          throw std::length_error("String too large");
        }
        compression_ptrs.push_back(
          reinterpret_cast<unsigned char const*>(sv.data()));
        compression_lens.push_back(sv.size());

        instructions.push_back(current_ptr_idx++);
      }
    }

    auto [impl_ptr, large_keys] = compress_core(compression_ptrs.size(),
      compression_lens.data(),
      compression_ptrs.data(),
      ratio.value);

    fsst_dictionary result_dict(std::move(impl_ptr));

    for (std::uint64_t val : instructions) {
      if (val & (1ULL << 63)) {
        emit_key(fsst_key{val});
      } else {
        emit_key(large_keys[static_cast<std::size_t>(val)]);
      }
    }

    return result_dict;
  }

  // --- Convenience Overloads ---

  std::pair<fsst_dictionary, std::vector<fsst_key>> fsst_dictionary::build(
    Generator gen, Deduplicator dedup, sample_ratio ratio)
  {
    std::vector<fsst_key> keys;
    auto                  dict = build(
      std::move(gen),
      std::move(dedup),
      [&keys](fsst_key k) { keys.push_back(k); },
      ratio);
    return {std::move(dict), std::move(keys)};
  }

  std::pair<fsst_dictionary, std::vector<fsst_key>>
  fsst_dictionary::build_from_unique(Generator gen, sample_ratio ratio)
  {
    std::vector<fsst_key> keys;
    auto                  dict = build_from_unique(
      std::move(gen), [&keys](fsst_key k) { keys.push_back(k); }, ratio);
    return {std::move(dict), std::move(keys)};
  }

} // namespace vault::algorithm
