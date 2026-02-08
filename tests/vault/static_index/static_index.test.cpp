/**
 * @file test_static_index.cpp
 * @brief Unit tests for vault::containers::static_index ensuring correctness
 * and builder pattern.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <vault/static_index/static_index.hpp>

using namespace vault::containers;

// --- Helper Functions ---

[[nodiscard]] std::vector<std::string> generate_unique_keys(
  size_t count, size_t len)
{
  static const char charset[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

  std::mt19937                          gen{std::random_device{}()};
  std::uniform_int_distribution<size_t> dist{0, sizeof(charset) - 2};

  std::set<std::string> unique_set;
  while (unique_set.size() < count) {
    std::string str(len, 0);
    std::generate(str.begin(), str.end(), [&]() { return charset[dist(gen)]; });
    unique_set.insert(str);
  }

  return {unique_set.begin(), unique_set.end()};
}

// --- Test Suite ---

TEST_CASE("StaticIndex: Core Lifecycle", "[static_index][core]")
{
  // 1. Setup Data
  const size_t num_keys = 5000;
  auto         keys     = generate_unique_keys(num_keys, 16);

  // 2. Build using the new Builder Pattern
  auto index = static_index_builder().add(keys).build();

  SECTION("Verify Hits")
  {
    for (const auto& key : keys) {
      auto result = index.lookup(key);
      REQUIRE(result.has_value());
    }
  }

  SECTION("Verify Misses")
  {
    auto missing_keys = generate_unique_keys(100, 17);
    for (const auto& key : missing_keys) {
      auto result = index.lookup(key);
      REQUIRE_FALSE(result.has_value());
    }
  }

  SECTION("Memory Usage Reporting")
  {
    size_t mem = index.memory_usage_bytes();
    REQUIRE(mem > 40000);
  }
}

TEST_CASE(
  "StaticIndex: Trait System Integration (Large N)", "[static_index][traits]")
{
  std::vector<int> int_keys;
  int_keys.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    int_keys.push_back(i * 10);
  }

  auto index = static_index_builder().add(int_keys).build();

  REQUIRE(index.lookup(10).has_value());
  REQUIRE(index.lookup(500).has_value()); // 50 * 10
  REQUIRE_FALSE(index.lookup(99).has_value());
}

TEST_CASE(
  "StaticIndex: Trait System Integration (Small N)", "[static_index][traits]")
{
  // Test that we can index a vector of integers directly
  std::vector<int> int_keys = {10, 20, 30, 40, 50};

  auto index = static_index_builder().add(int_keys).build();

  REQUIRE(index.lookup(10).has_value());
  REQUIRE(index.lookup(50).has_value());
  REQUIRE_FALSE(index.lookup(99).has_value());
}

TEST_CASE("StaticIndex: Move Semantics", "[static_index][memory]")
{
  auto keys = generate_unique_keys(1000, 10);

  auto source = static_index_builder().add(keys).build();
  REQUIRE(source.lookup(keys[0]).has_value());

  SECTION("Move Constructor")
  {
    static_index dest(std::move(source));

    REQUIRE(dest.lookup(keys[0]).has_value());
    // Source should be empty (reset)
    REQUIRE(source.empty());
  }

  SECTION("Move Assignment")
  {
    static_index dest; // Default constructs to empty
    dest = std::move(source);

    REQUIRE(dest.lookup(keys[0]).has_value());
    REQUIRE(source.empty());
  }
}

TEST_CASE("StaticIndex: Edge Cases", "[static_index][edge]")
{
  SECTION("Empty Vector Build")
  {
    std::vector<std::string> empty;

    auto index = static_index_builder().add(empty).build();

    REQUIRE(index.empty());
    REQUIRE_FALSE(index.lookup("anything").has_value());
  }
}
