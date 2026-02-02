// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <catch2/catch_test_macros.hpp>
#include <vault/algorithm/fsst_dictionary.hpp>

#include <iterator>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

using namespace vault::algorithm;

namespace {
  // Reusable deduplicator factory
  auto make_dedup()
  {
    auto map =
      std::make_shared<std::unordered_map<std::string_view, std::uint64_t>>();
    return [map](std::string_view s) -> std::pair<std::uint64_t, bool> {
      auto [it, inserted] = map->emplace(s, map->size());
      return {it->second, inserted};
    };
  }
} // namespace

TEST_CASE("fsst_dictionary core functionality", "[fsst][compression]")
{
  SECTION("Builds dictionary from basic strings and retrieves values")
  {
    auto const inputs = std::vector<std::string>{
      "apple_juice", "banana_bread", "cherry_pie", "date_fruit"};

    std::vector<fsst_key> keys;
    auto                  dict = fsst_dictionary::build(
      inputs, make_dedup(), [&](fsst_key k) { keys.push_back(k); });

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

    std::vector<fsst_key> keys;
    auto                  dict = fsst_dictionary::build(
      inputs, make_dedup(), [&](fsst_key k) { keys.push_back(k); });

    REQUIRE(keys.size() == 4);
    CHECK(keys[0] == keys[2]);
    CHECK(keys[0] == keys[3]);
    CHECK(keys[0] != keys[1]);
    CHECK(*dict[keys[0]] == "repeat_string");
  }

  SECTION("Handles empty input gracefully")
  {
    auto const            inputs = std::vector<std::string>{};
    std::vector<fsst_key> keys;
    auto                  dict = fsst_dictionary::build(
      inputs, make_dedup(), [&](fsst_key k) { keys.push_back(k); });

    CHECK(dict.empty());
    CHECK(keys.empty());
  }

  SECTION("Handles large strings forcing internal buffer resize")
  {
    auto const large_string = std::string(2048, 'A');
    auto const inputs       = std::vector<std::string>{large_string};

    std::vector<fsst_key> keys;
    auto                  dict = fsst_dictionary::build(
      inputs, make_dedup(), [&](fsst_key k) { keys.push_back(k); });

    REQUIRE(keys.size() == 1);
    auto result = dict[keys[0]];
    REQUIRE(result.has_value());
    CHECK(*result == large_string);
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

  SECTION("Works with projection (Arena Path)")
  {
    std::vector<fsst_key> keys;
    auto                  projected_range = records
      | std::ranges::views::transform(
        [](const user_record& r) { return r.username; });

    auto dict = fsst_dictionary::build(
      projected_range, make_dedup(), [&](fsst_key k) { keys.push_back(k); });

    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == keys[2]);
    CHECK(*dict[keys[1]] == "bob_builder");
  }

  SECTION("Works with non-contiguous range (Arena Path)")
  {
    std::list<std::vector<char>> inputs;
    std::string                  s1 = "list_item_1";
    std::string                  s2 = "list_item_2";
    inputs.push_back({s1.begin(), s1.end()});
    inputs.push_back({s2.begin(), s2.end()});

    std::vector<fsst_key> keys;
    auto                  dict = fsst_dictionary::build(
      inputs, make_dedup(), [&](fsst_key k) { keys.push_back(k); });

    REQUIRE(keys.size() == 2);
    CHECK(*dict[keys[0]] == "list_item_1");
    CHECK(*dict[keys[1]] == "list_item_2");
  }
}
