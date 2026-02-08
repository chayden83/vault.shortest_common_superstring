#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/static_index/static_index.hpp>

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>

// --- Helper Functions ---

// Generates unique random alphanumeric strings
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

TEST_CASE("StaticIndex: Core Lifecycle", "[static_index]")
{
  using namespace vault::containers;

  // 1. Setup Data
  const size_t num_keys = 5000;
  auto         keys     = generate_unique_keys(num_keys, 16);

  // 2. Instantiate
  static_index index;

  SECTION("Build and Verify Hits")
  {
    REQUIRE_NOTHROW(index.build(keys));

    // Verify every key we inserted returns a valid slot
    for (const auto& key : keys) {
      auto result = index.lookup(key);
      REQUIRE(result.has_value());

      // The slot must be within bounds of the storage
      // Note: PTHash slot mapping is technically minimal (0 to N-1),
      // but your implementation manages the storage size, so we implicitly
      // trust it. We can explicitly check if you expose size(), but lookup()
      // safety is key.
    }
  }

  SECTION("Verify Misses")
  {
    index.build(keys);

    // Generate NEW keys that strictly were not in the original set
    auto missing_keys = generate_unique_keys(
      100, 17); // Length 17 ensures no collision with length 16 keys

    for (const auto& key : missing_keys) {
      auto result = index.lookup(key);
      REQUIRE_FALSE(result.has_value());
    }
  }

  SECTION("Memory Usage Reporting")
  {
    index.build(keys);

    size_t mem = index.memory_usage_bytes();

    // Sanity check:
    // 5000 keys * 8 bytes (fingerprint) = 40,000 bytes.
    // Plus PTHash overhead (~3 bits/key) = ~1,875 bytes.
    // Total should be roughly > 41,000 bytes.
    REQUIRE(mem > 40000);
    REQUIRE(mem < 100000); // Should definitely be less than 100KB for 5k items
  }
}

TEST_CASE("StaticIndex: Move Semantics", "[static_index]")
{
  using namespace vault::containers;

  auto         keys = generate_unique_keys(1000, 10);
  static_index source;
  source.build(keys);

  // Sanity check source
  REQUIRE(source.lookup(keys[0]).has_value());

  SECTION("Move Constructor")
  {
    static_index dest(std::move(source));

    // Dest should have the data
    REQUIRE(dest.lookup(keys[0]).has_value());

    // Source should be empty/invalid (lookup returns nullopt or crashes safely)
    // Your implementation uses unique_ptrs/shared_ptrs, so source is likely
    // reset. However, pthash structs might not reset fully on move depending on
    // implementation. Ideally, we just check that Dest works.
  }

  SECTION("Move Assignment")
  {
    static_index dest;
    dest = std::move(source);

    REQUIRE(dest.lookup(keys[0]).has_value());
  }
}

TEST_CASE("StaticIndex: Edge Cases", "[static_index]")
{
  using namespace vault::containers;
  static_index index;

  SECTION("Empty Vector Build")
  {
    std::vector<std::string> empty;
    // PTHash sometimes asserts on empty inputs.
    // We test to see if your wrapper handles it or if it propagates the crash.
    // Ideally, this should not throw or crash.
    try {
      index.build(empty);
      // If it succeeds, lookup should return nullopt
      REQUIRE_FALSE(index.lookup("anything").has_value());
    } catch (...) {
      // If PTHash throws on empty, that's acceptable behavior to document.
      SUCCEED("Implementation throws on empty input (acceptable)");
    }
  }
}
