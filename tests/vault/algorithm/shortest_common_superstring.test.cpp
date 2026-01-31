// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <forward_list>
#include <iterator>
#include <list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vault/algorithm/internal.hpp> // Assumed available per instructions

#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>
#include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
#include <vault/algorithm/knuth_morris_pratt_searcher.hpp>
#include <vault/algorithm/shortest_common_superstring.hpp>

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

namespace val = vault::algorithm;

// --- Helper Structures for Advanced Tests ---

struct case_insensitive_eq {
  bool operator()(char a, char b) const
  {
    return std::tolower(static_cast<unsigned char>(a))
      == std::tolower(static_cast<unsigned char>(b));
  }
};

struct widget {
  int         id;
  std::string name;

  // Operator== required for vector verification
  bool operator==(const widget& other) const = default;
};

// --- Tests ---

TEST_CASE("knuth_morris_pratt_failure_function", "[kmp][utility]")
{
  auto expected_failure_function = std::vector{0, 0, 0, 0, 1, 2, 0};
  auto observed_failure_function =
    val::knuth_morris_pratt_failure_function("abcdabd"sv);

  CHECK(observed_failure_function == expected_failure_function);

  expected_failure_function = std::vector{0, 0, 1, 2, 0, 1, 2, 3, 4};
  observed_failure_function =
    val::knuth_morris_pratt_failure_function("ababcabab"sv);

  CHECK(observed_failure_function == expected_failure_function);
}

TEST_CASE("knuth_morris_pratt_searcher", "[kmp][utility]")
{
  auto const foo_searcher = val::knuth_morris_pratt_searcher("foo"sv);
  auto const bar_searcher = val::knuth_morris_pratt_searcher("bar"sv);

  auto const haystack = std::list{'f', 'o', 'o', 'b', 'a', 'r'};

  auto foo_pos = std::search(haystack.begin(), haystack.end(), foo_searcher);
  auto bar_pos = std::search(haystack.begin(), haystack.end(), bar_searcher);

  CHECK(foo_pos == std::next(haystack.begin(), 0));
  CHECK(bar_pos == std::next(haystack.begin(), 3));
}

TEST_CASE("knuth_morris_pratt_overlap", "[kmp][utility]")
{
  CHECK(val::knuth_morris_pratt_overlap("foobar"sv, "barstool"sv).score == 3);
}

TEST_CASE("shortest_common_superstring_basic", "[scs][basic]")
{
  using bounds_type = std::vector<std::pair<std::ptrdiff_t, std::size_t>>;

  SECTION("empty_range")
  {
    auto bounds = bounds_type{};
    auto input  = std::vector<std::string>{};

    auto [in, out, superstring, overlap] =
      val::shortest_common_superstring(input, std::back_inserter(bounds));

    CHECK(superstring.empty());
    CHECK(bounds.empty());
    CHECK(overlap == 0);
  }

  SECTION("singleton_range")
  {
    auto bounds = bounds_type{};
    auto input  = std::vector{"foobar"s};

    auto [in, out, superstring, overlap] =
      val::shortest_common_superstring(input, std::back_inserter(bounds));

    // Result is vector<char>, convert to string_view for comparison
    std::string_view sv(superstring.data(), superstring.size());
    CHECK(sv == "foobar"sv);
    CHECK(bounds.size() == 1);
  }
}

TEST_CASE("shortest_common_superstring_integration", "[scs][integration]")
{
  using bounds_type = std::vector<std::pair<std::ptrdiff_t, std::size_t>>;

  SECTION("bespoke_set")
  {
    auto input = std::vector{
      "bar"s,
      "foo"s,
      "door"s,
      "foobar"s,
      "bazfoo"s,
      "doorstop"s,
      "stoplight"s,
    };
    auto bounds = bounds_type{};

    auto [in, out, superstring, overlap] =
      val::shortest_common_superstring(input, std::back_inserter(bounds));

    // "bazfoobardoorstoplight" is length 22
    CHECK(superstring.size() == 22);

    // Reconstruct inputs from superstring bounds to verify correctness
    for (size_t i = 0; i < input.size(); ++i) {
      auto [offset, length] = bounds[i];
      REQUIRE(offset >= 0);
      REQUIRE(length == input[i].size());

      // Check that the character data matches
      bool match = std::ranges::equal(
        std::span(superstring).subspan(offset, length), input[i]);
      CHECK(match);
    }
  }

  SECTION("random_words_10k")
  {
    auto words = vault::internal::random_words_1k()
      | ::ranges::to<std::vector<std::string>>();

    auto bounds = bounds_type{};

    auto [in, out, superstring, overlap] =
      val::shortest_common_superstring(words, std::back_inserter(bounds));

    CHECK(overlap == 1636);
    CHECK(superstring.size() == 4790);

    // Verify reconstruction for a subset (e.g., first 100) to save time, or
    // all. Checking all as per original test intent:
    for (size_t i = 0; i < words.size(); ++i) {
      auto [offset, length] = bounds[i];
      bool match            = std::ranges::equal(
        std::span(superstring).subspan(offset, length), words[i]);
      CHECK(match);
    }
  }
}

TEST_CASE("shortest_common_superstring_advanced_features", "[scs][advanced]")
{
  SECTION("custom_comparator_case_insensitive")
  {
    // "FOO" overlaps "oobar" ('OO' == 'oo') -> "FOobar" (length 6)
    auto input = std::vector{"FOO"s, "oobar"s};
    std::vector<std::pair<std::ptrdiff_t, std::size_t>> bounds;

    auto result = val::shortest_common_superstring(
      input, std::back_inserter(bounds), case_insensitive_eq{});

    // FOO (3) + oobar (5) - overlap (2) = 6
    CHECK(result.superstring.size() == 6);

    // Verify contents: Should contain FOO and oobar logic
    std::string s(result.superstring.begin(), result.superstring.end());
    CHECK_THAT(s, Catch::Matchers::Equals("FOObar"));
  }

  SECTION("custom_projection_struct_member")
  {
    // Merge based on IDs: {1, 2, 3} + {3, 4, 5} -> {1, 2, 3, 4, 5}
    // Result is vector<int> because we project widgets to ints.
    auto input = std::vector<std::vector<widget>>{
      {{1, "A"}, {2, "B"}, {3, "C"}}, {{3, "Z"}, {4, "D"}, {5, "E"}}};

    std::vector<std::pair<std::ptrdiff_t, std::size_t>> bounds;

    auto result = val::shortest_common_superstring(input,
      std::back_inserter(bounds),
      [](const widget& w) { return w.id; } // Projection
    );

    // Check the resulting integer sequence
    std::vector<int> expected{1, 2, 3, 4, 5};

    CHECK(result.superstring == expected);
    CHECK(result.total_overlap == 1);
  }

  SECTION("integer_lists_non_string_container")
  {
    // Forward List of Vectors of Ints
    auto input = std::forward_list<std::vector<int>>{
      {1, 2, 3, 4}, {3, 4, 5, 6}, {5, 6, 7, 8}};
    std::vector<std::pair<std::ptrdiff_t, std::size_t>> bounds;

    auto result =
      val::shortest_common_superstring(input, std::back_inserter(bounds));

    // 1,2,3,4 + 3,4,5,6 (overlap 3,4) -> 1,2,3,4,5,6
    // + 5,6,7,8 (overlap 5,6) -> 1,2,3,4,5,6,7,8
    std::vector<int> expected{1, 2, 3, 4, 5, 6, 7, 8};

    CHECK(result.superstring == expected);
    CHECK(result.total_overlap == 4);
  }

  SECTION("floating_point_approximate_equality")
  {
    // Deque of doubles
    auto input =
      std::vector<std::deque<double>>{{1.0, 2.0000001, 3.0}, {3.0, 4.0, 5.0}};

    std::vector<std::pair<std::ptrdiff_t, std::size_t>> bounds;

    // Custom comparator for epsilon check
    auto approx_eq = [](double a, double b) { return std::abs(a - b) < 0.001; };

    auto result = val::shortest_common_superstring(
      input, std::back_inserter(bounds), approx_eq);

    // {1, 2, 3} overlaps {3, 4, 5} at '3' -> {1, 2, 3, 4, 5}
    CHECK(result.superstring.size() == 5);
    CHECK(result.total_overlap == 1);
    CHECK(std::abs(result.superstring[4] - 5.0) < 0.001);
  }
}
