// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <catch2/catch_test_macros.hpp>
#include <vault/algorithm/fsst_dictionary.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

using namespace vault::algorithm;

// Alias for convenience in tests (matches user's likely usage)
using StringDict = fsst_dictionary<std::string>;

namespace {
  auto make_dedup() -> fsst_dictionary_base::Deduplicator
  {
    auto map =
      std::make_shared<std::unordered_map<std::string_view, std::uint64_t>>();
    return [map](std::string_view s) -> std::pair<std::uint64_t, bool> {
      auto [it, inserted] = map->emplace(s, map->size());
      return {it->second, inserted};
    };
  }

  auto generate_strings(std::size_t count) -> std::vector<std::string>
  {
    auto result = std::vector<std::string>{};
    result.reserve(count);
    for (auto i = std::size_t{0}; i < count; ++i) {
      result.push_back("entry_" + std::to_string(i));
    }
    return result;
  }
} // namespace

// -----------------------------------------------------------------------------
// 1. Static Helper Tests (Key Mechanics)
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary static helpers", "[fsst][sso]")
{
  SECTION("is_inline_candidate identifies strings <= 7 bytes")
  {
    CHECK(fsst_dictionary_base::is_inline_candidate(""));
    CHECK(fsst_dictionary_base::is_inline_candidate("1234567"));
    CHECK_FALSE(fsst_dictionary_base::is_inline_candidate("12345678"));
  }

  SECTION("make_inline_key creates valid keys for short strings")
  {
    auto const s      = std::string{"tiny"};
    auto const k      = fsst_dictionary_base::make_inline_key(s);
    auto const dict   = StringDict{};
    auto const result = dict[k];
    REQUIRE(result.has_value());
    CHECK(*result == "tiny");
  }

  SECTION("make_inline_key throws on strings > 7 bytes")
  {
    CHECK_THROWS_AS(
      fsst_dictionary_base::make_inline_key("too_long"), std::length_error);
  }
}

// -----------------------------------------------------------------------------
// 2. Core Construction & Deduplication
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary construction strategies", "[fsst][build]")
{
  auto const inputs =
    std::vector<std::string>{"apple", "banana", "apple", "cherry", "banana"};

  SECTION("build() with deduplicator correctly maps duplicates")
  {
    auto keys = std::vector<fsst_key>{};
    // Use the Typed Dictionary Factory
    auto dict = StringDict::build(
      inputs, make_dedup(), [&](fsst_key k) { keys.push_back(k); });

    REQUIRE(keys.size() == 5);
    CHECK(keys[0] == keys[2]);
    CHECK(keys[1] == keys[4]);
    CHECK(keys[0] != keys[1]);

    CHECK(*dict[keys[0]] == "apple");
    CHECK(*dict[keys[1]] == "banana");
    CHECK(*dict[keys[3]] == "cherry");
  }

  SECTION("build_from_unique() treats every input as new")
  {
    auto keys = std::vector<fsst_key>{};
    auto dict = StringDict::build_from_unique(
      inputs, [&](fsst_key k) { keys.push_back(k); });

    REQUIRE(keys.size() == 5);

    auto const long_inputs =
      std::vector<std::string>{"long_string_value_1", "long_string_value_1"};
    auto long_keys = std::vector<fsst_key>{};
    auto long_dict = StringDict::build_from_unique(
      long_inputs, [&](fsst_key k) { long_keys.push_back(k); });

    CHECK(long_keys[0] != long_keys[1]);
    CHECK(*long_dict[long_keys[0]] == "long_string_value_1");
    CHECK(*long_dict[long_keys[1]] == "long_string_value_1");
  }

  SECTION("Convenience overloads returning std::pair")
  {
    auto [dict, keys] = StringDict::build(inputs, make_dedup());
    REQUIRE(keys.size() == 5);
    CHECK(*dict[keys[0]] == "apple");
  }
}

// -----------------------------------------------------------------------------
// 3. Boundary & Data Edge Cases
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary data boundaries", "[fsst][data]")
{
  SECTION("SSO Boundary (7 vs 8 bytes)")
  {
    auto const s7     = std::string("1234567");
    auto const s8     = std::string("12345678");
    auto const inputs = std::vector<std::string>{s7, s8};

    auto [dict, keys] = StringDict::build(inputs, make_dedup());

    REQUIRE(keys.size() == 2);
    CHECK(*dict[keys[0]] == s7);
    CHECK(*dict[keys[1]] == s8);
    CHECK(dict.size_in_bytes() > 0);
  }

  SECTION("Binary safety (embedded nulls)")
  {
    using namespace std::string_literals;
    auto const binary = "a\0b\0c"s;
    auto const inputs = std::vector<std::string>{binary};

    auto [dict, keys] = StringDict::build(inputs, make_dedup());

    REQUIRE(keys.size() == 1);
    auto const res = dict[keys[0]];
    REQUIRE(res.has_value());
    CHECK(res->size() == 5);
    CHECK(*res == binary);
  }

  SECTION("Empty string handling")
  {
    auto const inputs = std::vector<std::string>{"", "non-empty", ""};
    auto [dict, keys] = StringDict::build(inputs, make_dedup());

    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == keys[2]);
    CHECK(*dict[keys[0]] == "");
    CHECK(*dict[keys[1]] == "non-empty");
  }
}

// -----------------------------------------------------------------------------
// 4. Input Range Adapters
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary range adapters", "[fsst][ranges]")
{
  SECTION("Contiguous L-Value Range (std::vector<std::string>)")
  {
    auto const inputs = generate_strings(100);
    auto [dict, keys] = StringDict::build(inputs, make_dedup());
    CHECK(keys.size() == 100);
    CHECK(*dict[keys[99]] == "entry_99");
  }

  SECTION("Non-Contiguous Range (std::list<std::vector<char>>)")
  {
    auto list = std::list<std::vector<char>>{};
    list.push_back({'h', 'e', 'l', 'l', 'o'});
    list.push_back({'w', 'o', 'r', 'l', 'd'});

    auto [dict, keys] = StringDict::build(list, make_dedup());

    REQUIRE(keys.size() == 2);
    CHECK(*dict[keys[0]] == "hello");
    CHECK(*dict[keys[1]] == "world");
  }

  SECTION("R-Value Range (std::views::transform)")
  {
    auto const ints  = std::vector<int>{1, 2, 3};
    auto       range = ints | std::views::transform([](int i) {
      return "transformed_" + std::to_string(i);
    });

    auto [dict, keys] = StringDict::build(range, make_dedup());

    REQUIRE(keys.size() == 3);
    CHECK(*dict[keys[0]] == "transformed_1");
  }
}

// -----------------------------------------------------------------------------
// 5. Configuration (Levels & Ratios)
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary configuration", "[fsst][config]")
{
  auto const inputs = generate_strings(1000);

  SECTION("Explicit Sample Ratio")
  {
    auto ratio        = fsst_dictionary_base::sample_ratio{0.1f};
    auto [dict, keys] = StringDict::build(inputs, make_dedup(), ratio);
    CHECK(keys.size() == 1000);
  }

  SECTION("Compression Level (0-9)")
  {
    auto [dict0, keys0] = StringDict::build(
      inputs, make_dedup(), fsst_dictionary_base::compression_level{0});

    auto [dict9, keys9] = StringDict::build(
      inputs, make_dedup(), fsst_dictionary_base::compression_level{9});

    CHECK(keys0.size() == 1000);
    CHECK(keys9.size() == 1000);
  }

  SECTION("Invalid Sample Ratio throws")
  {
    auto bad_ratio = fsst_dictionary_base::sample_ratio{1.5f};
    CHECK_THROWS_AS(StringDict::build(inputs, make_dedup(), bad_ratio),
      std::invalid_argument);
  }
}

// -----------------------------------------------------------------------------
// 6. Rule of Five & Lifecycle
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary lifecycle", "[fsst][rule_of_5]")
{
  auto const inputs     = std::vector<std::string>{"persistence", "check"};
  auto [src_dict, keys] = StringDict::build(inputs, make_dedup());

  SECTION("Copy construction")
  {
    auto copy = src_dict;
    CHECK(*copy[keys[0]] == "persistence");
    CHECK(copy.size_in_bytes() == src_dict.size_in_bytes());
  }

  SECTION("Move construction")
  {
    auto moved = std::move(src_dict);
    CHECK(*moved[keys[0]] == "persistence");
    CHECK(src_dict.empty());
  }

  SECTION("Copy assignment")
  {
    auto other = StringDict{};
    other      = src_dict;
    CHECK(*other[keys[0]] == "persistence");
  }

  SECTION("Move assignment")
  {
    auto other = StringDict{};
    other      = std::move(src_dict);
    CHECK(*other[keys[0]] == "persistence");
    CHECK(src_dict.empty());
  }
}

// -----------------------------------------------------------------------------
// 7. Error Handling & Invariants
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary error handling", "[fsst][errors]")
{
  auto const inputs = std::vector<std::string>{"valid"};
  auto [dict, keys] = StringDict::build(inputs, make_dedup());

  SECTION("Out of bounds lookup returns nullopt")
  {
    auto large_inputs = generate_strings(1000);
    auto [large_dict, large_keys] =
      StringDict::build(large_inputs, make_dedup());

    // Using a valid pointer key from a large dict on a small dict guarantees
    // OOB.
    auto ptr_key = large_keys.back();
    auto result  = dict[ptr_key];
    CHECK_FALSE(result.has_value());
  }

  SECTION("Empty dictionary lookups")
  {
    auto empty_dict = StringDict{};
    CHECK(empty_dict.empty());
    CHECK(empty_dict.size_in_bytes() == 0);

    auto inline_key = fsst_dictionary_base::make_inline_key("hi");
    CHECK(*empty_dict[inline_key] == "hi");

    auto [d, k] = StringDict::build(
      std::vector<std::string>{"long_string_to_force_pointer"}, make_dedup());
    CHECK_FALSE(empty_dict[k[0]].has_value());
  }
}

// -----------------------------------------------------------------------------
// 8. try_find API (Generic & Reference)
// -----------------------------------------------------------------------------

TEST_CASE("fsst_dictionary try_find API", "[fsst][lookup]")
{
  auto const inputs = std::vector<std::string>{"apple", "banana", "cherry"};
  auto [dict, keys] = StringDict::build(inputs, make_dedup());

  SECTION("try_find (Value Overload) with std::string")
  {
    auto result = try_find<std::string>(dict, keys[0]);
    REQUIRE(result.has_value());
    CHECK(*result == "apple");
  }

  SECTION("try_find (Value Overload) with std::vector<char>")
  {
    auto result = try_find<std::vector<char>>(dict, keys[1]);
    REQUIRE(result.has_value());
    auto const expected = std::string("banana");
    CHECK(std::ranges::equal(*result, expected));
  }

  SECTION("try_find (Reference Overload) reuses capacity")
  {
    auto buffer = std::string{};
    buffer.reserve(128);
    auto const original_cap = buffer.capacity();

    // 1. First lookup
    auto found = try_find(dict, keys[2], buffer);
    CHECK(found);
    CHECK(buffer == "cherry");
    CHECK(buffer.capacity() >= original_cap); // Should reuse

    // 2. Second lookup (overwrite)
    found = try_find(dict, keys[0], buffer);
    CHECK(found);
    CHECK(buffer == "apple");
  }
}
