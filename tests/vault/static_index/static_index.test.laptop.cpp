#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include <vault/static_index/static_index.hpp>

using namespace vault::containers;

TEST_CASE("StaticIndex: Laptop Cache Hierarchy Analysis", "[benchmark][laptop]")
{
  std::mt19937_64 rng(2457498388); // Fixed seed

  auto generate_keys = [&](size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      keys.push_back("key_" + std::to_string(i) + "_" + std::to_string(rng()));
    }
    return keys;
  };

  SECTION("Small Index (L2 Resident - 100k)")
  {
    size_t num_keys = 100'000;
    auto   keys     = generate_keys(num_keys);

    static_index_builder builder;
    builder.add_n(keys);
    auto index = std::move(builder).build();

    std::vector<std::string> query_keys = keys;
    std::ranges::shuffle(query_keys, rng);

    BENCHMARK("Lookup L2 Resident")
    {
      size_t checksum = 0;
      for (const auto& key : query_keys) {
        if (index[key].has_value()) {
          checksum++;
        }
      }
      return checksum;
    };
  }

  SECTION("Medium Index (L3 Resident - 1M)")
  {
    size_t num_keys = 1'000'000;
    auto   keys     = generate_keys(num_keys);

    static_index_builder builder;
    builder.add_n(keys);
    auto index = std::move(builder).build();

    std::vector<std::string> query_keys = keys;
    std::ranges::shuffle(query_keys, rng);
    query_keys.resize(100'000);

    BENCHMARK("Lookup L3 Resident")
    {
      size_t checksum = 0;
      for (const auto& key : query_keys) {
        if (index[key].has_value()) {
          checksum++;
        }
      }
      return checksum;
    };
  }

  SECTION("Large Index (DRAM Bound - 20M)")
  {
    size_t num_keys = 20'000'000;

    UNSCOPED_INFO("Generating 20M keys...");
    auto keys = generate_keys(num_keys);

    UNSCOPED_INFO("Building 20M Index...");
    static_index_builder builder;
    builder.add_n(keys);
    auto index = std::move(builder).build();

    UNSCOPED_INFO("Benchmarking DRAM Bound Lookups...");

    std::vector<std::string> query_keys;
    query_keys.reserve(100'000);
    std::sample(
      keys.begin(), keys.end(), std::back_inserter(query_keys), 100'000, rng);

    BENCHMARK("Lookup DRAM Bound")
    {
      size_t checksum = 0;
      for (const auto& key : query_keys) {
        if (index[key].has_value()) {
          checksum++;
        }
      }
      return checksum;
    };
  }
}
