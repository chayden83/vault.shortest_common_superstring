#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/static_index/static_index.hpp>

#include <algorithm>
#include <climits>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace vault::containers;

// --- Advanced Generators ---

[[nodiscard]] std::vector<std::string> generate_dense_keys(size_t count)
{
  std::vector<std::string> keys;
  keys.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    // Generate dense numerical keys "0000001", "0000002"...
    // These often trigger different hashing edge cases than random strings
    keys.push_back(std::to_string(i));
  }
  return keys;
}

[[nodiscard]] std::vector<std::string> generate_binary_keys(
  size_t count, size_t size)
{
  std::vector<std::string> keys;
  keys.reserve(count);

  std::mt19937_64 rng(12345); // Fixed seed for reproducibility
  std::uniform_int_distribution<uint16_t> dist(0, 255);

  for (size_t i = 0; i < count; ++i) {
    std::string s(size, 0);
    for (size_t j = 0; j < size; ++j) {
      s[j] = static_cast<char>(dist(rng));
    }
    keys.push_back(std::move(s));
  }

  // Deduplicate to ensure valid input
  std::sort(keys.begin(), keys.end());
  auto last = std::unique(keys.begin(), keys.end());
  keys.erase(last, keys.end());

  return keys;
}

// --- The Extensive Suite ---

TEST_CASE("StaticIndex: Scale Stress Test (1M Items)", "[stress][slow]")
{
  // This ensures your refactor doesn't break on large datasets (e.g. integer
  // overflows)
  size_t const scale = 1'000'000;
  auto         keys  = generate_dense_keys(scale);

  static_index index;

  BENCHMARK("Build 1M Keys") { index.build(keys); };

  REQUIRE(index.memory_usage_bytes() > 0);

  // Verify a subset of keys to save test time, but spread them out
  // checking beginning, middle, and end
  SECTION("Verify Sample Correctness")
  {
    REQUIRE(index.lookup(keys[0]).has_value());
    REQUIRE(index.lookup(keys[scale / 2]).has_value());
    REQUIRE(index.lookup(keys[scale - 1]).has_value());
  }

  SECTION("Verify Non-Existent Keys (False Positive Check)")
  {
    // "1000001" was not generated
    REQUIRE_FALSE(index.lookup(std::to_string(scale + 1)).has_value());
  }
}

TEST_CASE("StaticIndex: Entropy & Edge Cases", "[edge]")
{
  static_index index;

  SECTION("Binary Data (Non-printable)")
  {
    // PTHash often assumes C-strings internally.
    // This ensures it correctly handles raw bytes including null terminators.
    auto keys = generate_binary_keys(10000, 32);

    // Inject a key with embedded nulls manually
    std::string null_key(16, 'A');
    null_key[5]  = '\0';
    null_key[10] = '\0';
    keys.push_back(null_key);

    index.build(keys);

    for (const auto& key : keys) {
      REQUIRE(index.lookup(key).has_value());
    }
  }

  SECTION("Avalanche / Bit-Flip Test")
  {
    // Ensure similar keys don't collide or confuse the lookup
    std::vector<std::string> keys;
    std::string              base(64, '0');
    keys.push_back(base);

    // Flip one bit at a time
    for (size_t i = 0; i < 64 * 8; ++i) {
      std::string copy = base;
      copy[i / 8] ^= (1 << (i % 8));
      keys.push_back(copy);
    }

    index.build(keys);

    for (const auto& key : keys) {
      REQUIRE(index.lookup(key).has_value());
    }
  }

  SECTION("Variable Length Keys")
  {
    std::vector<std::string> keys;
    for (size_t i = 1; i < 256; ++i) {
      keys.emplace_back(i, 'X'); // "X", "XX", "XXX"...
    }

    index.build(keys);

    for (const auto& key : keys) {
      REQUIRE(index.lookup(key).has_value());
    }
  }
}

TEST_CASE("StaticIndex: Performance Regression Baseline", "[benchmark]")
{
  // Run this BEFORE your refactor, record the numbers.
  // Run this AFTER your refactor.
  // If "Lookup Random" jumps from 30ns to 60ns, you broke the cache logic.

  size_t const count = 100'000;
  auto         keys  = generate_binary_keys(count, 16);
  static_index index;
  index.build(keys);

  // Prepare random queries (50% hits, 50% misses)
  std::vector<std::string> queries = keys;
  auto misses = generate_binary_keys(count, 17); // Length 17 = guaranteed miss
  queries.insert(queries.end(), misses.begin(), misses.end());

  std::mt19937 urng(42);
  std::shuffle(queries.begin(), queries.end(), urng);

  // We only benchmark the lookup, not the shuffling
  BENCHMARK_ADVANCED("Lookup 200k Mixed Items")(
    Catch::Benchmark::Chronometer meter)
  {
    meter.measure([&] {
      size_t hits = 0;
      for (const auto& q : queries) {
        if (index.lookup(q).has_value()) {
          hits++;
        }
      }
      return hits;
    });
  };
}
