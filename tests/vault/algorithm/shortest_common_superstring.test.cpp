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
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vault/algorithm/internal.hpp>

#include <vault/algorithm/knuth_morris_pratt_failure_function.hpp>
#include <vault/algorithm/knuth_morris_pratt_overlap.hpp>
#include <vault/algorithm/knuth_morris_pratt_searcher.hpp>
#include <vault/algorithm/shortest_common_superstring.hpp>

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

namespace val = vault::algorithm;

// --- Helper Structures ---

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
  bool        operator==(const widget& other) const = default;
};

// --- Tests ---

TEST_CASE("shortest_common_superstring_basic", "[scs][basic]")
{
  // The superstring is vector<char>. Bounds are subranges of
  // vector<char>::iterator.
  using subrange_type = std::ranges::subrange<std::vector<char>::iterator>;
  using bounds_type   = std::vector<subrange_type>;

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

    auto sv = std::string_view(superstring.data(), superstring.size());
    CHECK(sv == "foobar"sv);
    CHECK(bounds.size() == 1);
    CHECK(std::ranges::equal(bounds[0], "foobar"sv));
  }
}

TEST_CASE("shortest_common_superstring_integration", "[scs][integration]")
{
  using subrange_type = std::ranges::subrange<std::vector<char>::iterator>;
  using bounds_type   = std::vector<subrange_type>;

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

    CHECK(superstring.size() == 22);

    for (auto i = std::size_t{0}; i < input.size(); ++i) {
      // Verify content via the subrange directly
      CHECK(std::ranges::equal(bounds[i], input[i]));
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

    for (auto i = std::size_t{0}; i < words.size(); ++i) {
      CHECK(std::ranges::equal(bounds[i], words[i]));
    }
  }
}

TEST_CASE("shortest_common_superstring_advanced_features", "[scs][advanced]")
{

  SECTION("custom_comparator_case_insensitive")
  {
    using subrange_type = std::ranges::subrange<std::vector<char>::iterator>;
    auto input          = std::vector{"FOO"s, "oobar"s};
    auto bounds         = std::vector<subrange_type>{};

    auto result = val::shortest_common_superstring(
      input, std::back_inserter(bounds), case_insensitive_eq{});

    CHECK(result.superstring.size() == 6);
    auto s = std::string(result.superstring.begin(), result.superstring.end());
    CHECK_THAT(s, Catch::Matchers::Equals("FOObar"));
  }

  SECTION("custom_projection_struct_member")
  {
    // Result is vector<int>. Bounds are subranges of vector<int>::iterator.
    using subrange_type = std::ranges::subrange<std::vector<int>::iterator>;
    auto input          = std::vector<std::vector<widget>>{
      {{1, "A"}, {2, "B"}, {3, "C"}}, {{3, "Z"}, {4, "D"}, {5, "E"}}};

    auto bounds = std::vector<subrange_type>{};

    auto result = val::shortest_common_superstring(
      input, std::back_inserter(bounds), [](const widget& w) { return w.id; });

    auto expected = std::vector{1, 2, 3, 4, 5};
    CHECK(result.superstring == expected);

    // Check subranges
    auto expected_0 = std::vector{1, 2, 3};
    auto expected_1 = std::vector{3, 4, 5};
    CHECK(std::ranges::equal(bounds[0], expected_0));
    CHECK(std::ranges::equal(bounds[1], expected_1));
  }

  SECTION("integer_lists_non_string_container")
  {
    using subrange_type = std::ranges::subrange<std::vector<int>::iterator>;
    auto input          = std::forward_list<std::vector<int>>{
      {1, 2, 3, 4}, {3, 4, 5, 6}, {5, 6, 7, 8}};
    auto bounds = std::vector<subrange_type>{};

    auto result =
      val::shortest_common_superstring(input, std::back_inserter(bounds));

    auto expected = std::vector{1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(result.superstring == expected);

    // Check bounds match inputs
    auto in_vecs =
      std::vector<std::vector<int>>{{1, 2, 3, 4}, {3, 4, 5, 6}, {5, 6, 7, 8}};
    auto i = std::size_t{0};
    for (const auto& sub : bounds) {
      CHECK(std::ranges::equal(sub, in_vecs[i++]));
    }
  }
}
