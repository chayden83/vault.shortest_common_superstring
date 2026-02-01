// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <fsst.h>

namespace vault::algorithm {

  class fsst_dictionary::impl {
  public:
    std::vector<unsigned char> data_blob;
    fsst_decoder_t             decoder;

    impl() { std::memset(&decoder, 0, sizeof(decoder)); }
  };

  fsst_dictionary::fsst_dictionary()
      : pImpl(std::make_unique<impl>())
  {}

  fsst_dictionary::~fsst_dictionary()                          = default;
  fsst_dictionary::fsst_dictionary(fsst_dictionary&&) noexcept = default;
  fsst_dictionary& fsst_dictionary::operator=(
    fsst_dictionary&&) noexcept = default;

  fsst_dictionary::fsst_dictionary(std::unique_ptr<impl> implementation)
      : pImpl(std::move(implementation))
  {}

  bool fsst_dictionary::empty() const
  {
    return !pImpl || pImpl->data_blob.empty();
  }

  std::size_t fsst_dictionary::size_in_bytes() const
  {
    return pImpl ? pImpl->data_blob.size() : 0;
  }

  std::optional<std::string> fsst_dictionary::operator[](fsst_key key) const
  {
    if (empty()) {
      return std::nullopt;
    }
    if (key.offset + key.length > pImpl->data_blob.size()) {
      return std::nullopt;
    }

    auto* compressed_ptr = pImpl->data_blob.data() + key.offset;
    auto  compressed_len = key.length;

    auto result = std::string{};
    result.resize(std::max(std::size_t{64}, compressed_len * 5));

    auto actual_size = fsst_decompress(&pImpl->decoder,
      compressed_len,
      compressed_ptr,
      result.size(),
      reinterpret_cast<unsigned char*>(result.data()));

    result.resize(actual_size);
    return result;
  }

  // --- Static Factory Implementation (Core) ---

  fsst_dictionary fsst_dictionary::build(const std::vector<std::string>& inputs,
    std::function<void(fsst_key)> emit_key)
  {
    if (inputs.empty()) {
      return fsst_dictionary{};
    }

    // 1. Deduplicate
    auto unique_strings = inputs;
    std::ranges::sort(unique_strings);
    auto [last, end] = std::ranges::unique(unique_strings);
    unique_strings.erase(last, end);

    // 2. Prepare FSST Input
    auto str_ptrs = std::vector<unsigned char const*>{};
    auto str_lens = std::vector<std::size_t>{};
    str_ptrs.reserve(unique_strings.size());
    str_lens.reserve(unique_strings.size());

    for (auto& s : unique_strings) {
      // fsst_create expects unsigned char* (not const) in some versions,
      // but standard signature is usually const. We cast to be safe.
      str_ptrs.push_back(
        reinterpret_cast<unsigned char*>(const_cast<char*>(s.data())));
      str_lens.push_back(s.size());
    }

    // 3. Create Encoder
    auto* encoder =
      fsst_create(unique_strings.size(), str_lens.data(), str_ptrs.data(), 0);

    if (!encoder) {
      throw std::runtime_error("Failed to create FSST encoder");
    }

    // 4. Compress
    auto impl_ptr = std::make_unique<fsst_dictionary::impl>();

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

    for (auto i = std::size_t{0}; i < unique_strings.size(); ++i) {
      auto current_offset = blob.size();

      // Prepare 1-element arrays for batch API
      size_t src_len = unique_strings[i].size();
      auto*  src_ptr = reinterpret_cast<unsigned char const*>(
        const_cast<char*>(unique_strings[i].data()));

      const size_t*         lenIn = &src_len;
      unsigned char const** strIn = &src_ptr;

      size_t         dst_len = 0;
      unsigned char* dst_ptr = nullptr; // Will point inside compression_buffer

      // Call FSST with 8 arguments (Batch size = 1)
      fsst_compress(encoder,
        1,                         // nstrings
        lenIn,                     // lenIn
        strIn,                     // strIn
        compression_buffer.size(), // outsize
        compression_buffer.data(), // output
        &dst_len,                  // lenOut (receives compressed size)
        &dst_ptr                   // strOut (receives pointer)
      );

      blob.insert(blob.end(),
        compression_buffer.begin(),
        compression_buffer.begin() + dst_len);

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
    const std::vector<std::string>& inputs)
  {
    auto keys = std::vector<fsst_key>{};
    keys.reserve(inputs.size());

    // Delegate to the core build method
    auto dict = build(inputs, [&](fsst_key k) { keys.push_back(k); });

    return {std::move(dict), std::move(keys)};
  }

} // namespace vault::algorithm
