// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_ALGORITHM_HISTOGRAM_ALGORITHM_INTERNAL_HPP
#define VAULT_ALGORITHM_HISTOGRAM_ALGORITHM_INTERNAL_HPP

#include <span>
#include <string>
#include <vector>
#include <istream>

namespace vlt::internal {
  //using element_type = std::variant<std::monostate, std::string>;
  using element_type = std::string;

  std::span<element_type const> democracy_in_america   ();
  std::span<element_type const> democracy_and_education();

  std::vector<element_type> tokenize(std::istream &istream);

  inline std::vector<element_type> tokenize(std::istream &&istream) {
    return tokenize(istream);
  }
}

#endif
