// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <catch2/catch_test_macros.hpp>
#include <vault/algorithm/fsst_dictionary.hpp>

#include <string>
#include <unordered_map>
#include <vector>

using namespace vault::algorithm;

namespace {
  // Helper to keep tests concise since we removed the generic range build
  // overload
  auto build_simple(const std::vector<std::string>& inputs)
  {
    auto it  = inputs.begin();
    auto end = inputs.end();
    auto gen = [it, end]() mutable -> std::string const* {
      return it != end ? &(*it++) : nullptr;
    };

    auto map = std::unordered_map<std::string_view, std::size_t>{};
    auto dedup =
      [map = std::move(map)](
        std::string_view s) mutable -> std::pair<std::uint64_t, bool> {
      auto [iter, inserted] = map.emplace(s, map.size());
      return {iter->second, inserted};
    };

    return fsst_dictionary::build(std::move(gen), std::move(dedup));
  }
} // namespace

TEST_CASE("fsst_dictionary core functionality", "[fsst][compression]")
{
  SECTION("Builds dictionary from basic strings and retrieves values")
  {
    auto const inputs = std::vector<std::string>{
      "apple_juice", "banana_bread", "cherry_pie", "date_fruit"};

    auto [dict, keys] = build_simple(inputs);

    REQUIRE_FALSE(dict.empty());
    REQUIRE(dict.size_in_bytes() > 0);
    REQUIRE(keys.size() == inputs.size());

    for (auto i = std::size_t{0}; i < inputs.size(); ++i) {
      auto result = dict[keys[i]];
      REQUIRE(result.has_value());
      CHECK(*result == inputs[i]);
    }
  }

  SECTION("Handles deduplication of identical strings")
  {
    auto const inputs = std::vector<std::string>{
      "repeat_string", "unique_string", "repeat_string", "repeat_string"};

    auto [dict, keys] = build_simple(inputs);

    REQUIRE(keys.size() == 4);

    CHECK(keys[0] == keys[2]);
    CHECK(keys[0] == keys[3]);
    CHECK(keys[0] != keys[1]);

    CHECK(*dict[keys[0]] == "repeat_string");
    CHECK(*dict[keys[1]] == "unique_string");
  }

  SECTION("Handles empty input gracefully")
  {
    auto const inputs = std::vector<std::string>{};
    auto [dict, keys] = build_simple(inputs);

    CHECK(dict.empty());
    CHECK(dict.size_in_bytes() == 0);
    CHECK(keys.empty());
  }

  SECTION("Handles large strings forcing internal buffer resize")
  {
    auto const large_string = std::string(2048, 'A');
    auto const inputs       = std::vector<std::string>{large_string};

    auto [dict, keys] = build_simple(inputs);

    REQUIRE(keys.size() == 1);
    auto result = dict[keys[0]];

    REQUIRE(result.has_value());
    CHECK(*result == large_string);
    CHECK(result->size() == 2048);
  }
}

TEST_CASE("fsst_dictionary value semantics", "[fsst][lifecycle]")
{
  auto const inputs =
    std::vector<std::string>{"value_one_long", "value_two_long"};
  auto [original_dict, keys] = build_simple(inputs);

  SECTION("Copy construction shares underlying state")
  {
    auto copy_dict = original_dict; // Copy

    REQUIRE_FALSE(copy_dict.empty());
    CHECK(*copy_dict[keys[0]] == "value_one_long");
    CHECK(*original_dict[keys[0]] == "value_one_long");
    CHECK(copy_dict.size_in_bytes() == original_dict.size_in_bytes());
  }

  SECTION("Move construction transfers ownership")
  {
    auto moved_dict = std::move(original_dict);

    REQUIRE_FALSE(moved_dict.empty());
    CHECK(*moved_dict[keys[0]] == "value_one_long");
    CHECK(original_dict.empty());
  }

  SECTION("Copy assignment")
  {
    auto other_dict = fsst_dictionary{};
    other_dict      = original_dict;
    CHECK(*other_dict[keys[1]] == "value_two_long");
  }
}

TEST_CASE("fsst_dictionary template API", "[fsst][template]")
{
  struct user_record {
    int         id;
    std::string username;
  };

  auto const records = std::vector<user_record>{
    {1, "alice_wonderland"}, {2, "bob_builder"}, {3, "alice_wonderland"}};

  SECTION("Works with projection using RValueGenerator template")
  {
    auto keys = std::vector<fsst_key>{};
    auto it   = records.begin();
    auto end  = records.end();

    // RValue Generator returning optional<string>
    auto gen = [it, end]() mutable -> std::optional<std::string> {
      if (it == end) {
        return std::nullopt;
      }
      std::string val = it->username;
      ++it;
      return val;
    };

    // Manual deduplicator
    auto map = std::unordered_map<std::string_view, std::size_t>{};
    auto dedup =
      [map = std::move(map)](
        std::string_view s) mutable -> std::pair<std::uint64_t, bool> {
      auto [iter, inserted] = map.emplace(s, map.size());
      return {iter->second, inserted};
    };

    // Use the template build(RValueGenerator, Args...)
    // This internally buffers to vector and calls LValue overload
    auto dict = fsst_dictionary::build(
      std::move(gen), std::move(dedup), [&](fsst_key k) { keys.push_back(k); });

    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == keys[2]);
    CHECK(*dict[keys[1]] == "bob_builder");
  }
}
