#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vault/static_index/static_index.hpp>
#include <vector>

using namespace vault::containers;

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

  SECTION("Small Index (L2 Resident - 100k)")
  {
    size_t const count = 100'000;
    auto         keys  = generate_int_keys(count);

    auto index = static_index_builder().add(keys).build();

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

  SECTION("Medium Index (L3 Resident - 4M)")
  {
    size_t const count = 4'000'000;
    auto         keys  = generate_int_keys(count);

    auto index = static_index_builder().add(keys).build();

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

  SECTION("Large Index (DRAM Bound - 20M)")
  {
    size_t const count = 20'000'000;

    std::cout << "Generating 20M keys..." << std::endl;
    auto keys = generate_int_keys(count);

    std::cout << "Building 20M Index..." << std::endl;
    auto index = static_index_builder().add(keys).build();

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
