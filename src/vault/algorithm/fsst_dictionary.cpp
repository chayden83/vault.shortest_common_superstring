// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
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

    constexpr std::size_t kPointerLenShift = 40;
    constexpr std::size_t kPointerLenMask  = 0x7F'FFFF; // 23 bits
    constexpr std::size_t kPointerOffsetMask =
      0xFF'FFFF'FFFFULL; // 40 bits (Fixed literal)

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
    payload |= (1ULL << kInlineFlagShift); // Fixed literal
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

    // FIX: Force 8-byte alignment for fsst_export buffer
    alignas(8) unsigned char buf[FSST_MAXHEADER];
    fsst_export(encoder, buf);
    fsst_import(&impl_ptr->decoder, buf);

    impl_ptr->data_blob.reserve(count * 8);

    auto keys = std::vector<fsst_key>{};
    keys.reserve(count);

    auto max_len = std::size_t{0};
    for (size_t i = 0; i < count; ++i) {
      max_len = std::max(max_len, lens[i]);
    }

    std::size_t buffer_req = 2 * max_len + 16;
    // Buffer alignment is crucial for FSST UBSAN compliance
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

    fsst_destroy(encoder);
    return {std::move(impl_ptr), std::move(keys)};
  }

  // --- Factories (Delegators) ---

  fsst_dictionary fsst_dictionary::build(Generator gen,
    Deduplicator                                   dedup,
    std::function<void(fsst_key)>                  emit_key,
    sample_ratio                                   ratio)
  {
    std::vector<std::uint64_t> key_or_index;
    std::deque<std::string>    unique_large_strings;

    while (true) {
      auto opt_s = gen();
      if (!opt_s) {
        break;
      }

      std::string_view sv = *opt_s;

      if (is_inline_candidate(sv)) {
        fsst_key k = make_inline_key(sv);
        key_or_index.push_back(k.value);
      } else {
        // Store in stable deque first so string_view is valid
        unique_large_strings.emplace_back(std::move(*opt_s));
        std::string_view stable_view = unique_large_strings.back();

        auto [ret_idx, ret_is_new] = dedup(stable_view);

        if (ret_is_new) {
          key_or_index.push_back(ret_idx);
        } else {
          // Duplicate; remove local copy
          unique_large_strings.pop_back();
          key_or_index.push_back(ret_idx);
        }
      }
    }

    std::vector<unsigned char const*> ptrs;
    std::vector<std::size_t>          lens;
    ptrs.reserve(unique_large_strings.size());
    lens.reserve(unique_large_strings.size());

    for (const auto& s : unique_large_strings) {
      if (s.size() > kMaxPointerLength) {
        throw std::length_error("String too large");
      }
      ptrs.push_back(reinterpret_cast<unsigned char const*>(s.data()));
      lens.push_back(s.size());
    }

    auto [impl_ptr, large_keys] =
      compress_core(ptrs.size(), lens.data(), ptrs.data(), ratio.value);

    fsst_dictionary result_dict(std::move(impl_ptr));

    for (std::uint64_t val : key_or_index) {
      if (val & (1ULL << 63)) { // Fixed literal
        emit_key(fsst_key{val});
      } else {
        emit_key(large_keys[static_cast<std::size_t>(val)]);
      }
    }

    return result_dict;
  }

  std::pair<fsst_dictionary, std::vector<fsst_key>>
  fsst_dictionary::build_from_unique(
    std::span<std::string const> unique_inputs, sample_ratio ratio)
  {
    if (unique_inputs.empty()) {
      return {fsst_dictionary{}, {}};
    }

    auto ptrs = std::vector<unsigned char const*>{};
    auto lens = std::vector<std::size_t>{};
    ptrs.reserve(unique_inputs.size());
    lens.reserve(unique_inputs.size());

    for (size_t i = 0; i < unique_inputs.size(); ++i) {
      const auto& s = unique_inputs[i];
      if (is_inline_candidate(s)) {
        // skip
      } else {
        if (s.size() > kMaxPointerLength) {
          throw std::length_error("String too large");
        }
        ptrs.push_back(reinterpret_cast<unsigned char const*>(s.data()));
        lens.push_back(s.size());
      }
    }

    auto [impl_ptr, large_keys] =
      compress_core(ptrs.size(), lens.data(), ptrs.data(), ratio.value);

    std::vector<fsst_key> final_keys;
    final_keys.reserve(unique_inputs.size());

    size_t large_counter = 0;
    for (const auto& s : unique_inputs) {
      if (is_inline_candidate(s)) {
        final_keys.push_back(make_inline_key(s));
      } else {
        final_keys.push_back(large_keys[large_counter++]);
      }
    }

    return {fsst_dictionary(std::move(impl_ptr)), std::move(final_keys)};
  }

  std::pair<fsst_dictionary, std::vector<fsst_key>>
  fsst_dictionary::build_from_unique(
    std::span<std::string const> unique_inputs, compression_level level)
  {
    static constexpr auto max_compression_level =
      std::ranges::size(compression_levels);

    level.value = std::clamp(level.value, 0uz, max_compression_level - 1);
    sample_ratio ratio{compression_levels[level.value].value};
    return build_from_unique(unique_inputs, ratio);
  }

  fsst_dictionary fsst_dictionary::build(std::span<std::string const> inputs,
    std::function<void(fsst_key)>                                     emit_key,
    sample_ratio                                                      ratio)
  {
    if (inputs.empty()) {
      return fsst_dictionary{};
    }

    auto unique = inputs | std::ranges::to<std::vector>();
    std::ranges::sort(unique);
    auto [last, end] = std::ranges::unique(unique);
    unique.erase(last, end);

    auto [dict, keys] = build_from_unique(unique, ratio);

    if (emit_key) {
      for (const auto& s : inputs) {
        auto it = std::lower_bound(unique.begin(), unique.end(), s);
        emit_key(keys[std::distance(unique.begin(), it)]);
      }
    }
    return dict;
  }

  fsst_dictionary fsst_dictionary::build(std::span<std::string const> inputs,
    std::function<void(fsst_key)>                                     emit_key,
    compression_level                                                 level)
  {
    static constexpr auto max_compression_level =
      std::ranges::size(compression_levels);
    level.value = std::clamp(level.value, 0uz, max_compression_level - 1);
    sample_ratio ratio{compression_levels[level.value].value};
    return build(inputs, emit_key, ratio);
  }

  std::pair<fsst_dictionary, std::vector<fsst_key>> fsst_dictionary::build(
    std::span<std::string const> inputs, sample_ratio ratio)
  {
    auto keys = std::vector<fsst_key>{};
    keys.reserve(inputs.size());
    auto dict = build(inputs, [&](fsst_key k) { keys.push_back(k); }, ratio);
    return {std::move(dict), std::move(keys)};
  }

  std::pair<fsst_dictionary, std::vector<fsst_key>> fsst_dictionary::build(
    std::span<std::string const> inputs, compression_level level)
  {
    static constexpr auto max_compression_level =
      std::ranges::size(compression_levels);
    level.value = std::clamp(level.value, 0uz, max_compression_level - 1);
    sample_ratio ratio{compression_levels[level.value].value};
    return build(inputs, ratio);
  }

} // namespace vault::algorithm
