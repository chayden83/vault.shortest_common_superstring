// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

#include <list>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
#include <string_view>

#include <vault/algorithm/internal.hpp>

#include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
#include <vault/algorithm/knuth_morris_pratt_searcher.hpp>
#include <vault/algorithm/shortest_common_superstring.hpp>


// clang-format off

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

namespace val = vault::algorithm;

TEST(KnuthMorrisPrattFailureFunction, ShortestCommonSuperstring) {
  auto expected_failure_function =  std::vector { 0, 0, 0, 0, 1, 2, 0 };

  auto observed_failure_function =
    val::knuth_morris_pratt_failure_function("abcdabd"sv);

  EXPECT_EQ(observed_failure_function, expected_failure_function);

  expected_failure_function = std::vector { 0, 0, 1, 2, 0, 1, 2, 3, 4 };

  observed_failure_function =
    val::knuth_morris_pratt_failure_function("ababcabab"sv);

  EXPECT_EQ(observed_failure_function, expected_failure_function);
}

TEST(KnuthMorrisPrattSearcher, ShortestCommonSuperstring) {
  auto const foo_searcher = val::knuth_morris_pratt_searcher("foo"sv);
  auto const bar_searcher = val::knuth_morris_pratt_searcher("bar"sv);

  auto const haystack = std::list { 'f', 'o', 'o', 'b', 'a', 'r' };

  auto foo_pos = std::search(haystack.begin(), haystack.end(), foo_searcher);
  auto bar_pos = std::search(haystack.begin(), haystack.end(), bar_searcher);

  EXPECT_EQ(foo_pos, (std::next(haystack.begin(), 0)));
  EXPECT_EQ(bar_pos, (std::next(haystack.begin(), 3)));
}

TEST(KnuthMorrisPrattOverlap, ShortestCommonSuperstring) {
  EXPECT_EQ(val::knuth_morris_pratt_overlap("foobar"sv, "barstool"sv).score, 3);
}

TEST(ShortestCommonSuperstringOfEmptyRange, ShortestCommonSuperstring) {
  auto bounds = std::vector<std::pair<std::ptrdiff_t, std::size_t>> { };

  auto [in, out, superstring, overlap] = val::shortest_common_superstring
    (std::vector<std::string> { }, std::back_inserter(bounds));

  EXPECT_EQ(superstring, ""sv);
  EXPECT_TRUE(bounds.empty());
}

TEST(ShortestCommonSuperstringOfSingletonRange, ShortestCommonSuperstring) {
  auto bounds = std::vector<std::pair<std::ptrdiff_t, std::size_t>> { };

  auto [in, out, superstring, overlap] = val::shortest_common_superstring
    (std::vector { "foobar"s }, std::back_inserter(bounds));

  EXPECT_EQ(superstring, "foobar"sv);
}

TEST(ShortestCommonSuperstringBespoke, ShortestCommonSuperstring) {
  auto input = std::vector {
    "bar"s, "foo"s, "door"s, "foobar"s, "bazfoo"s, "doorstop"s, "stoplight"s,
  };

  auto bounds = std::vector<std::pair<std::ptrdiff_t, std::size_t>> { };

  auto [in, out, superstring, overlap] = val::shortest_common_superstring
    (input, std::back_inserter(bounds));

  EXPECT_EQ(superstring, "bazfoobardoorstoplight");

  auto reconstructed = bounds | ::ranges::views::transform
    ([&](auto const &b) { return superstring.substr(b.first, b.second); });

  EXPECT_THAT(reconstructed | ::ranges::to<std::vector>(), ::testing::ContainerEq(input));
}

TEST(ShortestCommonSuperstring1K, ShortestCommonSuperstring) {
  auto words = vault::internal::random_words_1k()
    | ::ranges::to<std::vector<std::string>>();

  auto bounds = std::vector<std::pair<std::ptrdiff_t, std::size_t>> { };

  auto [in, out, superstring, overlap] = val::shortest_common_superstring
    (words, std::back_inserter(bounds));

  EXPECT_EQ(overlap             ,  896);
  EXPECT_EQ(superstring.length(), 4665);

  auto reconstructed = bounds | ::ranges::views::transform
    ([&](auto const &b) { return superstring.substr(b.first, b.second); });

  EXPECT_THAT(reconstructed | ::ranges::to<std::vector>(), ::testing::ContainerEq(words));
}

// clang-format on
