// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>

#include <list>
#include <string>
#include <iterator>
#include <algorithm>
#include <string_view>

#include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
#include <vault/algorithm/knuth_morris_pratt_searcher.hpp>
#include <vault/algorithm/shortest_common_superstring.hpp>

// clang-format off

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

TEST(KnuthMorrisPrattFailureFunction, ShortestCommonSuperstring) {
  auto expected_failure_function =  std::vector { 0, 0, 0, 0, 1, 2, 0 };

  auto observed_failure_function =
    vault::algorithm::knuth_morris_pratt_failure_function("abcdabd"sv);

  EXPECT_EQ(observed_failure_function, expected_failure_function);

  expected_failure_function = std::vector { 0, 0, 1, 2, 0, 1, 2, 3, 4 };

  observed_failure_function =
    vault::algorithm::knuth_morris_pratt_failure_function("ababcabab"sv);

  EXPECT_EQ(observed_failure_function, expected_failure_function);  
}

TEST(KnuthMorrisPrattSearcher, ShortestCommonSuperstring) {
  auto const foo_searcher =
    vault::algorithm::knuth_morris_pratt_searcher("foo"sv);
  auto const bar_searcher =
    vault::algorithm::knuth_morris_pratt_searcher("bar"sv);

  auto const haystack = std::list { 'f', 'o', 'o', 'b', 'a', 'r' };

  auto foo_pos = std::search(haystack.begin(), haystack.end(), foo_searcher);
  auto bar_pos = std::search(haystack.begin(), haystack.end(), bar_searcher);

  EXPECT_EQ(foo_pos, (std::next(haystack.begin(), 0)));
  EXPECT_EQ(bar_pos, (std::next(haystack.begin(), 3)));
}

TEST(KnuthMorrisPrattOverlap, ShortestCommonSuperstring) {
  auto result = vault::algorithm::knuth_morris_pratt_overlap
    ("foobar"sv, "barstool"sv);

  EXPECT_EQ(result.score, 3);
}

TEST(ShortestCommonSuperstring, ShortestCommonSuperstring) {
  auto input = std::vector {
    "bar"s, "foo"s, "door"s, "foobar"s, "bazfoo"s, "doorstop"s, "stoplight"s,
  };
  
  auto bounds = std::vector<std::pair<std::ptrdiff_t, std::size_t>> { };
  
  auto [in, out, superstring] = vault::algorithm::shortest_common_superstring
    (input, std::back_inserter(bounds));
  
  EXPECT_EQ(superstring, "bazfoobardoorstoplight");

  auto reconstructed = bounds | ::ranges::views::transform
    ([&](auto const &b) { return superstring.substr(b.first, b.second); });

  EXPECT_EQ(reconstructed | ::ranges::to<std::vector>(), input);
}

// clang-format on
