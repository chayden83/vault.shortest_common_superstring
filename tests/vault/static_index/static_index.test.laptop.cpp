#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <vault/static_index/static_index.hpp>

using namespace vault::containers;

// Helper to generate keys without blowing up RAM
std::vector<std::string> generate_int_keys(size_t count)
{
  std::vector<std::string> keys;
  keys.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    keys.push_back(std::to_string(i));
  }
  return keys;
}

TEST_CASE("StaticIndex: Laptop Cache Hierarchy Analysis", "[benchmark][laptop]")
{

  // 1. L2 Cache Resident (Fastest)
  //    Index Size: ~40 KB
  //    Your L2 cache is likely 256 KB per core. This fits easily.
  SECTION("Small Index (L2 Resident - 100k)")
  {
    size_t const count = 100'000;
    auto         keys  = generate_int_keys(count);

    static_index index;
    index.build(keys);

    // Shuffle queries to defeat hardware prefetchers
    std::mt19937             urng(42);
    std::vector<std::string> queries = keys;
    std::shuffle(queries.begin(), queries.end(), urng);

    BENCHMARK("Lookup L2 Resident")
    {
      size_t hits = 0;
      for (const auto& key : queries) {
        if (index.lookup(key).has_value()) {
          hits++;
        }
      }
      return hits;
    };
  }

  // 2. L3 Cache Resident (Fast)
  //    Index Size: ~1.5 MB
  //    Your L3 cache is likely 4MB - 6MB. This fits comfortably.
  SECTION("Medium Index (L3 Resident - 4M)")
  {
    size_t const count = 4'000'000;
    // Requires ~200MB RAM for construction
    auto keys = generate_int_keys(count);

    static_index index;
    index.build(keys);

    // Create a random access pattern
    size_t const             query_count = 100'000;
    std::vector<std::string> queries;
    queries.reserve(query_count);
    std::mt19937                          urng(12345);
    std::uniform_int_distribution<size_t> dist(0, count - 1);

    for (size_t i = 0; i < query_count; ++i) {
      queries.push_back(keys[dist(urng)]);
    }

    BENCHMARK("Lookup L3 Resident")
    {
      size_t hits = 0;
      for (const auto& key : queries) {
        if (index.lookup(key).has_value()) {
          hits++;
        }
      }
      return hits;
    };
  }

  // 3. L3 Thrashing (Slow - DRAM Bound)
  //    Index Size: ~8.5 MB
  //    This is GUARANTEED to exceed a 6MB L3 cache.
  //    The "Pilot" values will constantly be evicted, forcing DRAM reads.
  SECTION("Large Index (DRAM Bound - 20M)")
  {
    size_t const count = 20'000'000;

    // Requires ~1.2 GB RAM for construction.
    // Should be safe for an 8GB laptop.
    std::cout << "Generating 20M keys..." << std::endl;
    auto keys = generate_int_keys(count);

    std::cout << "Building 20M Index (May take 5-10s on older CPU)..."
              << std::endl;
    static_index index;
    index.build(keys);

    size_t const             query_count = 100'000;
    std::vector<std::string> queries;
    queries.reserve(query_count);
    std::mt19937                          urng(12345);
    std::uniform_int_distribution<size_t> dist(0, count - 1);

    for (size_t i = 0; i < query_count; ++i) {
      queries.push_back(keys[dist(urng)]);
    }

    std::cout << "Benchmarking DRAM Bound Lookups..." << std::endl;
    BENCHMARK("Lookup DRAM Bound")
    {
      size_t hits = 0;
      for (const auto& key : queries) {
        if (index.lookup(key).has_value()) {
          hits++;
        }
      }
      return hits;
    };
  }
}
