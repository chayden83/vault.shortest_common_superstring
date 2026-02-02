// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

#include <fsst.h>

namespace vault::algorithm {

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
    if (empty()) {
      return std::nullopt;
    }
    if (key.offset + key.length > p_impl->data_blob.size()) {
      return std::nullopt;
    }

    auto const* compressed_ptr = p_impl->data_blob.data() + key.offset;
    auto        compressed_len = key.length;

    auto result = std::string{};
    result.resize(std::max(std::size_t{64}, compressed_len * 5));

    auto actual_size =
      fsst_decompress(const_cast<fsst_decoder_t*>(&p_impl->decoder),
        compressed_len,
        const_cast<unsigned char*>(compressed_ptr),
        result.size(),
        reinterpret_cast<unsigned char*>(result.data()));

    result.resize(actual_size);
    return result;
  }

  // --- Private Static Helper: Core FSST Logic ---

  std::pair<std::shared_ptr<fsst_dictionary::impl>, std::vector<fsst_key>>
  fsst_dictionary::compress_core(std::size_t count,
    std::size_t*                             lens,
    unsigned char const**                    ptrs,
    float                                    sample_ratio)
  {
    if (sample_ratio <= 0.0f || sample_ratio > 1.0f) {
      throw std::invalid_argument("sample_ratio must be in (0, 1]");
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

    unsigned char buf[FSST_MAXHEADER];
    fsst_export(encoder, buf);
    fsst_import(&impl_ptr->decoder, buf);

    impl_ptr->data_blob.reserve(count * 8);

    auto keys = std::vector<fsst_key>{};
    keys.reserve(count);

    auto compression_buffer = std::vector<unsigned char>{};
    auto max_len            = std::size_t{0};
    for (size_t i = 0; i < count; ++i) {
      max_len = std::max(max_len, lens[i]);
    }
    compression_buffer.resize(2 * max_len + 16);

    constexpr auto MAX_OFFSET = std::size_t{1} << 40;
    constexpr auto MAX_LENGTH = std::size_t{1} << 24;

    for (size_t i = 0; i < count; ++i) {
      if (impl_ptr->data_blob.size() >= MAX_OFFSET) {
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
        compression_buffer.size(),
        compression_buffer.data(),
        &dst_len,
        &dst_ptr);

      if (dst_len >= MAX_LENGTH) {
        fsst_destroy(encoder);
        throw std::length_error("fsst_dictionary: Compressed string limit");
      }

      auto offset = impl_ptr->data_blob.size();
      impl_ptr->data_blob.insert(impl_ptr->data_blob.end(),
        compression_buffer.begin(),
        compression_buffer.begin() + dst_len);

      keys.push_back({offset, dst_len});
    }

    fsst_destroy(encoder);
    return {std::move(impl_ptr), std::move(keys)};
  }

  // --- Factory: Build from Unique ---

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

    constexpr auto MAX_LEN = std::size_t{1} << 24;

    for (const auto& s : unique_inputs) {
      if (s.size() >= MAX_LEN) {
        throw std::length_error("String too large");
      }
      ptrs.push_back(reinterpret_cast<unsigned char const*>(s.data()));
      lens.push_back(s.size());
    }

    auto [impl_ptr, keys] = compress_core(
      unique_inputs.size(), lens.data(), ptrs.data(), ratio.value);

    return {fsst_dictionary(std::move(impl_ptr)), std::move(keys)};
  }

  std::pair<fsst_dictionary, std::vector<fsst_key>>
  fsst_dictionary::build_from_unique(
    std::span<std::string const> unique_inputs, compression_level level)
  {
    static constexpr auto max_compression_level =
      std::ranges::size(compression_levels);

    level.value = std::clamp(level.value, 0uz, max_compression_level - 1);

    // We now construct a sample_ratio from the level's value
    sample_ratio ratio{compression_levels[level.value].value};
    return build_from_unique(unique_inputs, ratio);
  }

  // --- Factory: Standard Build (Sort/Unique) ---

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
