// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <fstream>
#include <ranges>
#include <string>
#include <vector>

#include <vault/algorithm/internal.hpp>

// clang-format off

namespace vlt::internal {
  std::vector<element_type> tokenize(std::istream &istream) {
    return std::ranges::to<std::vector<element_type>>
      (std::views::istream<std::string>(istream));
  }

  std::span<element_type const> democracy_in_america() {
    static auto const storage = tokenize
      (std::ifstream { PROJECT_DATA_DIR "/democracy_in_america.txt" });

    return storage;
  }    

  std::span<element_type const> democracy_and_education() {
    static auto const storage = tokenize
      (std::ifstream{PROJECT_DATA_DIR "/democracy_and_education.txt"});

    return storage;
  }    
}

// clang-format on
