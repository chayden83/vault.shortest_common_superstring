// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

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
    // Heuristic resize: 4-5x compression ratio assumption or min 64
    // bytes
    result.resize(std::max(std::size_t{64}, compressed_len * 5));

    // Note: fsst_decompress signature takes non-const decoder pointer
    // in C API but effectively reads it. We may need a const_cast if
    // the C API is not const-correct, but standard usage implies
    // read-only decoder usage here.
    auto actual_size =
      fsst_decompress(const_cast<fsst_decoder_t*>(&p_impl->decoder),
        compressed_len,
        const_cast<unsigned char*>(compressed_ptr), // API quirk
        result.size(),
        reinterpret_cast<unsigned char*>(result.data()));

    result.resize(actual_size);
    return result;
  }

  // --- Static Factory Implementation (Core) ---

  fsst_dictionary fsst_dictionary::build(
    std::span<std::string const> inputs, std::function<void(fsst_key)> emit_key)
  {
    if (inputs.empty()) {
      return fsst_dictionary{};
    }

    // 1. Deduplicate
    auto unique_strings = inputs | std::ranges::to<std::vector>();
    std::ranges::sort(unique_strings);
    auto [last, end] = std::ranges::unique(unique_strings);
    unique_strings.erase(last, end);

    // 2. Prepare FSST Input
    auto str_ptrs = std::vector<unsigned char const*>{};
    auto str_lens = std::vector<std::size_t>{};
    str_ptrs.reserve(unique_strings.size());
    str_lens.reserve(unique_strings.size());

    // Limits for bit-field packing
    constexpr auto MAX_OFFSET = std::size_t{1} << 40; // 1 TB
    constexpr auto MAX_LENGTH = std::size_t{1} << 24; // 16 MB

    for (const auto& s : unique_strings) {
      if (s.size() >= MAX_LENGTH) {
        throw std::length_error(
          "fsst_dictionary: String too large for key format");
      }
      str_ptrs.push_back(reinterpret_cast<unsigned char const*>(s.data()));
      str_lens.push_back(s.size());
    }

    // 3. Create Encoder
    // fsst_create takes non-const pointers in standard API signature, but
    // we pass our correctly typed const pointer array.
    auto* encoder =
      fsst_create(unique_strings.size(), str_lens.data(), str_ptrs.data(), 0);

    if (!encoder) {
      throw std::runtime_error("Failed to create FSST encoder");
    }

    // 4. Compress
    // We construct a mutable impl locally
    auto impl_ptr = std::make_shared<impl>();

    unsigned char deserialized_buf[FSST_MAXHEADER];
    auto          header_size = fsst_export(encoder, deserialized_buf);
    fsst_import(&impl_ptr->decoder, deserialized_buf);

    auto& blob = impl_ptr->data_blob;
    blob.reserve(inputs.size() * 16);

    auto compression_buffer = std::vector<unsigned char>{};
    auto max_input_size     = std::size_t{0};
    for (auto len : str_lens) {
      max_input_size = std::max(max_input_size, len);
    }
    compression_buffer.resize(2 * max_input_size + 16);

    auto unique_keys = std::vector<fsst_key>{};
    unique_keys.reserve(unique_strings.size());

    for (const auto& s : unique_strings) {
      auto current_offset = blob.size();

      if (current_offset >= MAX_OFFSET) {
        fsst_destroy(encoder);
        throw std::length_error(
          "fsst_dictionary: Blob size exceeds key format");
      }

      // Prepare 1-element arrays for batch API
      auto src_len = s.size();
      auto src_ptr = reinterpret_cast<unsigned char const*>(s.data());

      auto const*           len_in = &src_len;
      unsigned char const** str_in = &src_ptr;

      auto  dst_len = std::size_t{0};
      auto* dst_ptr = static_cast<unsigned char*>(nullptr);

      // Pass str_in directly (unsigned char const**)
      fsst_compress(encoder,
        1,                         // nstrings
        len_in,                    // lenIn
        str_in,                    // strIn API non-const quirk
        compression_buffer.size(), // outsize
        compression_buffer.data(), // output
        &dst_len,                  // lenOut
        &dst_ptr                   // strOut
      );

      if (dst_len >= MAX_LENGTH) {
        fsst_destroy(encoder);
        throw std::length_error("fsst_dictionary: Compressed string too large");
      }

      blob.insert(blob.end(),
        compression_buffer.begin(),
        compression_buffer.begin() + dst_len);

      // Initialize with struct initializer syntax
      unique_keys.push_back({current_offset, dst_len});
    }

    fsst_destroy(encoder);

    // 5. Map inputs to keys
    if (emit_key) {
      for (const auto& s : inputs) {
        auto it =
          std::lower_bound(unique_strings.begin(), unique_strings.end(), s);
        auto index =
          static_cast<std::size_t>(std::distance(unique_strings.begin(), it));
        emit_key(unique_keys[index]);
      }
    }

    return fsst_dictionary(std::move(impl_ptr));
  }

  // --- Static Factory Implementation (Convenience Overload) ---

  std::pair<fsst_dictionary, std::vector<fsst_key>> fsst_dictionary::build(
    std::span<std::string const> inputs)
  {
    auto keys = std::vector<fsst_key>{};
    keys.reserve(inputs.size());

    auto dict = build(inputs, [&](fsst_key k) { keys.push_back(k); });

    return {std::move(dict), std::move(keys)};
  }

} // namespace vault::algorithm
