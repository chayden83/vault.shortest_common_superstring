// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_INTERNAL_HPP
#define VAULT_ALGORITHM_SHORTEST_COMMON_SUPERSTRING_INTERNAL_HPP

#include <span>

namespace vault::internal {
  std::span<char const* const> random_words_1k();
  std::span<char const* const> random_words_10k();
} // namespace vault::internal

#endif
