#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric> // for std::iota
#include <random>
#include <string>
#include <vector>

#include <vault/static_index/static_index.hpp>

using namespace vault::containers;

TEST_CASE("StaticIndex: Extensive correctness and collision tests",
  "[static_index][extensive]")
{
  std::mt19937_64 rng(999999);

  SECTION("Exact Membership (String Keys)")
  {
    std::vector<std::string> keys;
    for (int i = 0; i < 5000; ++i) {
      keys.push_back(
        std::to_string(i) + "_test_value_" + std::to_string(rng()));
    }

    static_index_builder builder;
    builder.add_n(keys);
    auto index = std::move(builder).build();

    // Positive Lookups
    for (const auto& key : keys) {
      auto res = index[key];
      REQUIRE(res.has_value());
    }

    // Negative Lookups
    for (int i = 0; i < 1000; ++i) {
      std::string missing =
        "missing_" + std::to_string(i) + "_" + std::to_string(rng());
      auto res = index[missing];
      REQUIRE_FALSE(res.has_value());
    }
  }

  SECTION("Integer Keys (Direct Byte Sequence)")
  {
    std::vector<uint64_t> keys;
    for (uint64_t i = 0; i < 10000; ++i) {
      keys.push_back(i * 1000 + 7);
    }

    static_index_builder builder;
    builder.add_n(keys);
    auto index = std::move(builder).build();

    for (auto k : keys) {
      REQUIRE(index[k].has_value());
    }

    REQUIRE_FALSE(index[99999999ULL].has_value());
  }

  SECTION("Permutation Verification (Random Shuffle)")
  {
    size_t                count = 10000;
    std::vector<uint64_t> input_data(count);
    std::iota(input_data.begin(), input_data.end(), 0);
    std::ranges::shuffle(input_data, rng);

    static_index_builder builder;
    builder.add_n(input_data);

    std::vector<size_t> perm;
    perm.reserve(count);

    // Use the sink API to capture permutation
    auto [index, _] = std::move(builder).build(std::back_inserter(perm));

    REQUIRE(perm.size() == count);

    // Verify that for every input item, index[item] == perm[original_index]
    for (size_t i = 0; i < count; ++i) {
      auto slot = index[input_data[i]];
      REQUIRE(slot.has_value());
      REQUIRE(*slot == perm[i]);
    }
  }
}

TEST_CASE("StaticIndex: Performance Regression Baseline", "[benchmark]")
{
  std::mt19937_64 rng(12345);
  size_t          count = 200'000;

  std::vector<std::string> strings;
  strings.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    strings.push_back(std::to_string(rng()));
  }

  static_index_builder builder;
  builder.add_n(strings);
  auto index = std::move(builder).build();

  std::ranges::shuffle(strings, rng);

  BENCHMARK("Lookup 200k Mixed Items")
  {
    size_t checksum = 0;
    for (const auto& s : strings) {
      if (index[s].has_value()) {
        checksum++;
      }
    }
    return checksum;
  };
}
