#include <algorithm>
#include <cassert>
#include <iterator>

#include <vault/frozen_vector/frozen_vector_builder.hpp>
#include <vault/string_arena/string_arena.hpp>

namespace vault::arena {

  [[nodiscard]] auto to_arena(std::function<void(chars_sink&)> chars_source)
    -> std::pair<std::vector<string>, frozen::frozen_vector<char>> {
    assert(chars_source && "The character source function must not be empty.");

    // Internal metadata to track string extents prior to final allocation.
    struct pending_string_info {
      std::size_t size;
      std::size_t offset;
      char        inline_data[max_inline_size];
    };

    auto temp_buffer     = frozen::frozen_vector_builder<char>{};
    auto pending_strings = std::vector<pending_string_info>{};

    auto sink_impl = [&temp_buffer, &pending_strings](std::span<char const> chars) {
      auto pending = pending_string_info{chars.size(), 0, {}};

      if (chars.size() <= max_inline_size) {
        std::copy(chars.begin(), chars.end(), pending.inline_data);
      } else {
        pending.offset = temp_buffer.size();

        for (auto c : chars) {
          temp_buffer.push_back(c);
        }

        temp_buffer.push_back('\0');
      }

      pending_strings.push_back(pending);
    };

    auto sink = chars_sink{sink_impl};

    // Pass 1: Accumulate metadata and long-string characters.
    chars_source(sink);

    // Pass 2: Allocate the final shared memory block.
    auto shared_buffer = std::move(temp_buffer).freeze();

    // Pass 3: Construct the final, immutable string instances.
    auto result_strings = std::vector<string>{};
    result_strings.reserve(pending_strings.size());

    for (auto const& pending : pending_strings) {
      if (pending.size <= max_inline_size) {
        result_strings.emplace_back(pending.inline_data, pending.size);
      } else {
        auto const* data_ptr = shared_buffer.data() + pending.offset;
        result_strings.emplace_back(data_ptr, pending.size);
      }
    }

    return {std::move(result_strings), std::move(shared_buffer)};
  }

} // namespace vault::arena
