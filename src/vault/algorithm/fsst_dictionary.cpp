// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <deque>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

#include <fsst.h>

namespace vault::algorithm {

  namespace {
    // --- Key Layout Constants ---
    constexpr auto kInlineFlagShift = 63uz;
    constexpr auto kInlineLenShift  = 56uz;
    constexpr auto kInlineLenMask   = 0x7Fuz;

    constexpr auto kPointerLenShift   = 40uz;
    constexpr auto kPointerLenMask    = 0x7F'FFFFuz;
    constexpr auto kPointerOffsetMask = 0xFF'FFFF'FFFFULL;

    constexpr auto kByteMask = 0xFFuz;

    constexpr auto kMaxPointerLength = kPointerLenMask;
    constexpr auto kMaxPointerOffset = kPointerOffsetMask;
    constexpr auto kMaxInlineLength  = 7uz;

    // --- Internal Helpers ---

    auto create_pointer_key(std::size_t offset, std::size_t length) -> fsst_key
    {
      if (length > kMaxPointerLength) {
        throw std::length_error("fsst_key: string exceeds 8MB limit");
      }
      auto const payload =
        (static_cast<std::uint64_t>(length) << kPointerLenShift)
        | (offset & kPointerOffsetMask);

      assert((payload & (1ULL << kInlineFlagShift)) == 0);
      return fsst_key{payload};
    }

    auto key_is_inline(fsst_key k) -> bool
    {
      return (k.value >> kInlineFlagShift) & 1;
    }

    auto extract_inline_string(fsst_key k) -> std::string
    {
      auto       s   = std::string{};
      auto const len = (k.value >> kInlineLenShift) & kInlineLenMask;

      assert(len <= kMaxInlineLength);
      s.resize(len);

      for (auto i = std::size_t{0}; i < len; ++i) {
        s[i] = static_cast<char>((k.value >> (i * 8)) & kByteMask);
      }
      return s;
    }

    auto decode_pointer_key(fsst_key k) -> std::pair<std::size_t, std::size_t>
    {
      auto const len = (k.value >> kPointerLenShift) & kPointerLenMask;
      auto const off = k.value & kPointerOffsetMask;
      return {off, len};
    }

    // --- R-Value Buffering Adapter ---
    // Takes an RValueGenerator and a lambda that accepts a ViewGenerator.
    // Manages the lifetime of a stable buffer for the duration of the lambda
    // call.
    template <typename Func>
    auto buffered_build_adapter(
      fsst_dictionary::RValueGenerator gen, Func&& func)
    {
      auto stable_buffer = std::deque<std::string>{};

      auto view_gen = [&]() mutable -> std::optional<std::string_view> {
        auto opt_s = gen();
        if (!opt_s) {
          return std::nullopt;
        }
        stable_buffer.push_back(std::move(*opt_s));
        return std::string_view{stable_buffer.back()};
      };

      return func(std::move(view_gen));
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

    auto payload = std::uint64_t{0};
    for (auto i = std::size_t{0}; i < s.size(); ++i) {
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

    auto const actual_size =
      fsst_decompress(const_cast<fsst_decoder_t*>(&p_impl->decoder),
        length,
        const_cast<unsigned char*>(compressed_ptr),
        result.size(),
        reinterpret_cast<unsigned char*>(result.data()));

    result.resize(actual_size);
    return result;
  }

  // --- Core FSST Compression ---

  auto fsst_dictionary::compress_core(std::size_t count,
    std::size_t*                                  lens,
    unsigned char const**                         ptrs,
    float                                         sample_ratio)
    -> std::pair<std::shared_ptr<impl>, std::vector<fsst_key>>
  {
    if (sample_ratio <= 0.0f || sample_ratio > 1.0f) {
      throw std::invalid_argument("sample_ratio must be in (0, 1]");
    }
    if (count == 0) {
      return {std::make_shared<fsst_dictionary::impl>(), {}};
    }

    auto* encoder = static_cast<fsst_encoder_t*>(nullptr);
    auto  target_samples =
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

      for (auto const idx : sampled_indices) {
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

    auto total_input_size = std::size_t{0};
    auto max_len          = std::size_t{0};
    for (auto i = std::size_t{0}; i < count; ++i) {
      total_input_size += lens[i];
      max_len = std::max(max_len, lens[i]);
    }

    impl_ptr->data_blob.reserve(total_input_size + 1024);

    auto keys = std::vector<fsst_key>{};
    keys.reserve(count);

    auto const buffer_req = 2 * max_len + 16;
    auto aligned_buf = std::vector<unsigned long long>((buffer_req + 7) / 8);
    unsigned char* comp_buf_ptr =
      reinterpret_cast<unsigned char*>(aligned_buf.data());
    auto const comp_buf_len = aligned_buf.size() * 8;

    for (auto i = std::size_t{0}; i < count; ++i) {
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

      auto const offset = impl_ptr->data_blob.size();

      impl_ptr->data_blob.insert(
        impl_ptr->data_blob.end(), comp_buf_ptr, comp_buf_ptr + dst_len);

      keys.push_back(create_pointer_key(offset, dst_len));
    }

    impl_ptr->data_blob.shrink_to_fit();

    fsst_destroy(encoder);
    return {std::move(impl_ptr), std::move(keys)};
  }

  // --- Factories: View-Based (Zero Copy) ---

  auto fsst_dictionary::build(Generator gen,
    Deduplicator                        dedup,
    std::function<void(fsst_key)>       emit_key,
    sample_ratio                        ratio) -> fsst_dictionary
  {
    auto instructions     = std::vector<std::uint64_t>{};
    auto compression_ptrs = std::vector<unsigned char const*>{};
    auto compression_lens = std::vector<std::size_t>{};

    while (true) {
      auto opt_sv = gen();
      if (!opt_sv) {
        break;
      }

      auto const sv = *opt_sv;

      if (is_inline_candidate(sv)) {
        auto const k = make_inline_key(sv);
        instructions.push_back(k.value);
      } else {
        auto const [idx, is_new] = dedup(sv);

        if (is_new) {
          if (sv.size() > kMaxPointerLength) {
            throw std::length_error("String too large");
          }
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

    auto result_dict = fsst_dictionary(std::move(impl_ptr));

    for (auto const val : instructions) {
      if (val & (1ULL << kInlineFlagShift)) {
        emit_key(fsst_key{val});
      } else {
        emit_key(large_keys[static_cast<std::size_t>(val)]);
      }
    }

    return result_dict;
  }

  auto fsst_dictionary::build_from_unique(
    Generator gen, std::function<void(fsst_key)> emit_key, sample_ratio ratio)
    -> fsst_dictionary
  {
    auto instructions     = std::vector<std::uint64_t>{};
    auto compression_ptrs = std::vector<unsigned char const*>{};
    auto compression_lens = std::vector<std::size_t>{};
    auto current_ptr_idx  = std::size_t{0};

    while (true) {
      auto opt_sv = gen();
      if (!opt_sv) {
        break;
      }

      auto const sv = *opt_sv;

      if (is_inline_candidate(sv)) {
        auto const k = make_inline_key(sv);
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

    auto result_dict = fsst_dictionary(std::move(impl_ptr));

    for (auto const val : instructions) {
      if (val & (1ULL << kInlineFlagShift)) {
        emit_key(fsst_key{val});
      } else {
        emit_key(large_keys[static_cast<std::size_t>(val)]);
      }
    }

    return result_dict;
  }

  // --- Factories: R-Value Based (Stable Buffer) ---

  auto fsst_dictionary::build(RValueGenerator gen,
    Deduplicator                              dedup,
    std::function<void(fsst_key)>             emit_key,
    sample_ratio                              ratio) -> fsst_dictionary
  {
    return buffered_build_adapter(std::move(gen), [&](Generator view_gen) {
      return build(
        std::move(view_gen), std::move(dedup), std::move(emit_key), ratio);
    });
  }

  auto fsst_dictionary::build_from_unique(RValueGenerator gen,
    std::function<void(fsst_key)>                         emit_key,
    sample_ratio ratio) -> fsst_dictionary
  {
    return buffered_build_adapter(std::move(gen), [&](Generator view_gen) {
      return build_from_unique(std::move(view_gen), std::move(emit_key), ratio);
    });
  }

  // --- Convenience Overloads (View) ---

  auto fsst_dictionary::build(
    Generator gen, Deduplicator dedup, sample_ratio ratio)
    -> std::pair<fsst_dictionary, std::vector<fsst_key>>
  {
    auto keys = std::vector<fsst_key>{};
    auto dict = build(
      std::move(gen),
      std::move(dedup),
      [&keys](fsst_key k) { keys.push_back(k); },
      ratio);
    return {std::move(dict), std::move(keys)};
  }

  auto fsst_dictionary::build_from_unique(Generator gen, sample_ratio ratio)
    -> std::pair<fsst_dictionary, std::vector<fsst_key>>
  {
    auto keys = std::vector<fsst_key>{};
    auto dict = build_from_unique(
      std::move(gen), [&keys](fsst_key k) { keys.push_back(k); }, ratio);
    return {std::move(dict), std::move(keys)};
  }

  // --- Convenience Overloads (R-Value) ---

  auto fsst_dictionary::build(
    RValueGenerator gen, Deduplicator dedup, sample_ratio ratio)
    -> std::pair<fsst_dictionary, std::vector<fsst_key>>
  {
    return buffered_build_adapter(std::move(gen), [&](Generator view_gen) {
      return build(std::move(view_gen), std::move(dedup), ratio);
    });
  }

  auto fsst_dictionary::build_from_unique(RValueGenerator gen,
    sample_ratio ratio) -> std::pair<fsst_dictionary, std::vector<fsst_key>>
  {
    return buffered_build_adapter(std::move(gen), [&](Generator view_gen) {
      return build_from_unique(std::move(view_gen), ratio);
    });
  }

} // namespace vault::algorithm
