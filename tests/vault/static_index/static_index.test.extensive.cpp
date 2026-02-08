#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/static_index/static_index.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace vault::containers;

// --- Generators ---

[[nodiscard]] std::vector<std::string> generate_dense_keys(size_t count)
{
  std::vector<std::string> keys;
  keys.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    keys.push_back(std::to_string(i));
  }
  return keys;
}

[[nodiscard]] std::vector<std::string> generate_binary_keys(
  size_t count, size_t size)
{
  std::vector<std::string> keys;
  keys.reserve(count);
  std::mt19937_64                         rng(12345);
  std::uniform_int_distribution<uint16_t> dist(0, 255);

  for (size_t i = 0; i < count; ++i) {
    std::string s(size, 0);
    for (size_t j = 0; j < size; ++j) {
      s[j] = static_cast<char>(dist(rng));
    }
    keys.push_back(std::move(s));
  }
  std::sort(keys.begin(), keys.end());
  auto last = std::unique(keys.begin(), keys.end());
  keys.erase(last, keys.end());
  return keys;
}

// --- The Extensive Suite ---

TEST_CASE("StaticIndex: Scale Stress Test (1M Items)", "[stress][slow]")
{
  size_t const scale = 1'000'000;
  auto         keys  = generate_dense_keys(scale);

  // We declare the index outside to keep it alive for the checks
  std::optional<static_index> index_opt;

  BENCHMARK("Build 1M Keys")
  {
    return static_index_builder().add_n(keys).build();
  };

  // Actually build it for verification
  auto index = static_index_builder().add_n(keys).build();

  REQUIRE(index.memory_usage_bytes() > 0);

  SECTION("Verify Sample Correctness")
  {
    REQUIRE(index.lookup(keys[0]).has_value());
    REQUIRE(index.lookup(keys[scale / 2]).has_value());
    REQUIRE(index.lookup(keys[scale - 1]).has_value());
  }

  SECTION("Verify Non-Existent Keys")
  {
    REQUIRE_FALSE(index.lookup(std::to_string(scale + 1)).has_value());
  }
}

TEST_CASE("StaticIndex: Entropy & Edge Cases", "[edge]")
{
  SECTION("Binary Data (Non-printable)")
  {
    auto        keys = generate_binary_keys(10000, 32);
    std::string null_key(16, 'A');
    null_key[5]  = '\0';
    null_key[10] = '\0';
    keys.push_back(null_key);

    auto index = static_index_builder().add_n(keys).build();

    for (const auto& key : keys) {
      REQUIRE(index.lookup(key).has_value());
    }
  }

  SECTION("Avalanche / Bit-Flip Test")
  {
    std::vector<std::string> keys;
    std::string              base(64, '0');
    keys.push_back(base);

    for (size_t i = 0; i < 64 * 8; ++i) {
      std::string copy = base;
      copy[i / 8] ^= (1 << (i % 8));
      keys.push_back(copy);
    }

    auto index = static_index_builder().add_n(keys).build();

    for (const auto& key : keys) {
      REQUIRE(index.lookup(key).has_value());
    }
  }
}

TEST_CASE("StaticIndex: Performance Regression Baseline", "[benchmark]")
{
  size_t const count = 100'000;
  auto         keys  = generate_binary_keys(count, 16);

  auto index = static_index_builder().add_n(keys).build();

  std::vector<std::string> queries = keys;
  auto                     misses  = generate_binary_keys(count, 17);
  queries.insert(queries.end(), misses.begin(), misses.end());

  std::mt19937 urng(42);
  std::shuffle(queries.begin(), queries.end(), urng);

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
