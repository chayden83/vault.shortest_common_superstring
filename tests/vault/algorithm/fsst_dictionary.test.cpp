// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <catch2/catch_test_macros.hpp>
#include <vault/algorithm/fsst_dictionary.hpp>

#include <iterator>
#include <string>
#include <vector>

using namespace vault::algorithm;

TEST_CASE("fsst_dictionary core functionality", "[fsst][compression]")
{

  SECTION("Builds dictionary from basic strings and retrieves values")
  {
    // UPDATED: Used strings > 7 bytes to ensure they are NOT inlined (SSO)
    // so we can test the actual dictionary blob mechanics.
    auto const inputs = std::vector<std::string>{
      "apple_juice", "banana_bread", "cherry_pie", "date_fruit"};

    auto [dict, keys] = fsst_dictionary::build(inputs);

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

    auto [dict, keys] = fsst_dictionary::build(inputs);

    REQUIRE(keys.size() == 4);

    // key[0], key[2], and key[3] should all point to "repeat_string"
    CHECK(keys[0] == keys[2]);
    CHECK(keys[0] == keys[3]);

    // key[1] should be different
    CHECK(keys[0] != keys[1]);

    CHECK(*dict[keys[0]] == "repeat_string");
    CHECK(*dict[keys[1]] == "unique_string");
  }

  SECTION("Handles empty input gracefully")
  {
    auto const inputs = std::vector<std::string>{};
    auto [dict, keys] = fsst_dictionary::build(inputs);

    CHECK(dict.empty());
    CHECK(dict.size_in_bytes() == 0);
    CHECK(keys.empty());
  }

  SECTION("Handles large strings forcing internal buffer resize")
  {
    auto const large_string = std::string(2048, 'A');
    auto const inputs       = std::vector<std::string>{large_string};

    auto [dict, keys] = fsst_dictionary::build(inputs);

    REQUIRE(keys.size() == 1);
    auto result = dict[keys[0]];

    REQUIRE(result.has_value());
    CHECK(*result == large_string);
    CHECK(result->size() == 2048);
  }
}

TEST_CASE("fsst_dictionary value semantics", "[fsst][lifecycle]")
{
  // UPDATED: Use strings > 7 bytes
  auto const inputs =
    std::vector<std::string>{"value_one_long", "value_two_long"};
  auto [original_dict, keys] = fsst_dictionary::build(inputs);

  SECTION("Copy construction shares underlying state")
  {
    auto copy_dict = original_dict; // Copy

    REQUIRE_FALSE(copy_dict.empty());
    CHECK(*copy_dict[keys[0]] == "value_one_long");
    CHECK(*original_dict[keys[0]] == "value_one_long");

    // Since it uses shared_ptr<impl const>, verify state is shared
    CHECK(copy_dict.size_in_bytes() == original_dict.size_in_bytes());
  }

  SECTION("Move construction transfers ownership")
  {
    auto moved_dict = std::move(original_dict);

    REQUIRE_FALSE(moved_dict.empty());
    CHECK(*moved_dict[keys[0]] == "value_one_long");

    // Original should be empty after move
    CHECK(original_dict.empty());
    CHECK(original_dict.size_in_bytes() == 0);
  }

  SECTION("Copy assignment")
  {
    auto other_dict = fsst_dictionary{};
    other_dict      = original_dict;

    CHECK(*other_dict[keys[1]] == "value_two_long");
  }
}

TEST_CASE("fsst_dictionary error handling", "[fsst][error]")
{
  auto const inputs = std::vector<std::string>{"test_long_string"};
  auto [dict, keys] = fsst_dictionary::build(inputs);

  SECTION("Returns nullopt for out-of-bounds keys")
  {
    // Create an invalid key manually.
    // We must ensure the MSB is 0 to force a lookup (pointer key)
    // 0x7FFFFFFF is safe (MSB=0)
    fsst_key bad_key;
    bad_key.value = 0x7FFFFFFF;

    auto result = dict[bad_key];
    CHECK_FALSE(result.has_value());
  }

  SECTION("Returns nullopt for default constructed dictionary")
  {
    auto     empty_dict = fsst_dictionary{};
    fsst_key some_key;
    some_key.value = 0; // Pointer to 0,0

    CHECK_FALSE(empty_dict[some_key].has_value());
  }
}

TEST_CASE("make_fsst_dictionary template API", "[fsst][template]")
{
  struct user_record {
    int         id;
    std::string username;
  };

  auto const records = std::vector<user_record>{
    {1, "alice_wonderland"}, {2, "bob_builder"}, {3, "alice_wonderland"}};

  SECTION("Works with projection")
  {
    auto keys = std::vector<fsst_key>{};

    auto dict = make_fsst_dictionary(records,
      std::back_inserter(keys),
      &user_record::username // Projection
    );

    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == keys[2]); // Deduplication check
    CHECK(*dict[keys[1]] == "bob_builder");
  }
}
