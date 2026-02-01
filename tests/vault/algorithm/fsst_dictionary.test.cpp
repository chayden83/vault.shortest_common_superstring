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
    auto const inputs =
      std::vector<std::string>{"apple", "banana", "cherry", "date"};

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
    auto const inputs =
      std::vector<std::string>{"repeat", "unique", "repeat", "repeat"};

    auto [dict, keys] = fsst_dictionary::build(inputs);

    REQUIRE(keys.size() == 4);

    // key[0], key[2], and key[3] should all point to "repeat"
    CHECK(keys[0] == keys[2]);
    CHECK(keys[0] == keys[3]);

    // key[1] should be different
    CHECK(keys[0] != keys[1]);

    CHECK(*dict[keys[0]] == "repeat");
    CHECK(*dict[keys[1]] == "unique");
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
    // FSST usually operates on smaller strings, but we should verify the
    // retrieval logic handles buffers larger than the initial heuristic.
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
  auto const inputs          = std::vector<std::string>{"value1", "value2"};
  auto [original_dict, keys] = fsst_dictionary::build(inputs);

  SECTION("Copy construction shares underlying state")
  {
    auto copy_dict = original_dict; // Copy

    REQUIRE_FALSE(copy_dict.empty());
    CHECK(*copy_dict[keys[0]] == "value1");
    CHECK(*original_dict[keys[0]] == "value1");

    // Since it uses shared_ptr<impl const>, verify state is shared
    // (observable via size_in_bytes returning identical values)
    CHECK(copy_dict.size_in_bytes() == original_dict.size_in_bytes());
  }

  SECTION("Move construction transfers ownership")
  {
    auto moved_dict = std::move(original_dict);

    REQUIRE_FALSE(moved_dict.empty());
    CHECK(*moved_dict[keys[0]] == "value1");

    // Original should be empty after move
    CHECK(original_dict.empty());
    CHECK(original_dict.size_in_bytes() == 0);
  }

  SECTION("Copy assignment")
  {
    auto other_dict = fsst_dictionary{};
    other_dict      = original_dict;

    CHECK(*other_dict[keys[1]] == "value2");
  }
}

TEST_CASE("fsst_dictionary error handling", "[fsst][error]")
{
  auto const inputs = std::vector<std::string>{"test"};
  auto [dict, keys] = fsst_dictionary::build(inputs);

  SECTION("Returns nullopt for out-of-bounds keys")
  {
    // Create an invalid key manually
    auto bad_key = fsst_key{.offset = dict.size_in_bytes() + 100, .length = 5};

    auto result = dict[bad_key];
    CHECK_FALSE(result.has_value());
  }

  SECTION("Returns nullopt for default constructed dictionary")
  {
    auto empty_dict = fsst_dictionary{};
    auto some_key   = fsst_key{0, 5};

    CHECK_FALSE(empty_dict[some_key].has_value());
  }
}

TEST_CASE("fsst_compress_strings template API", "[fsst][template]")
{
  struct user_record {
    int         id;
    std::string username;
  };

  auto const records = std::vector<user_record>{
    {1, "alice"}, {2, "bob"}, {3, "alice"} // "alice" repeated
  };

  SECTION("Works with projection")
  {
    auto keys = std::vector<fsst_key>{};

    auto dict = fsst_compress_strings(records,
      std::back_inserter(keys),
      &user_record::username // Projection
    );

    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == keys[2]); // Deduplication check
    CHECK(*dict[keys[1]] == "bob");
  }

  SECTION("Works with raw ranges")
  {
    auto const raw  = std::vector<std::string>{"one", "two"};
    auto       keys = std::vector<fsst_key>{};

    auto dict = fsst_compress_strings(raw, std::back_inserter(keys));

    REQUIRE(keys.size() == 2);
    CHECK(*dict[keys[0]] == "one");
  }
}
