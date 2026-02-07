#include <algorithm>
#include <benchmark/benchmark.h>
#include <boost/unordered/unordered_flat_set.hpp>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vault/static_index/static_index.hpp>
#include <vector>

// Configuration
static const size_t key_len = 16;

// Helper: Generate random keys
std::vector<std::string> generate_keys(size_t count)
{
  std::vector<std::string> keys;
  keys.reserve(count);

  std::mt19937_64                         rng(42);
  std::uniform_int_distribution<uint64_t> dist;

  for (size_t i = 0; i < count; ++i) {
    std::string s;
    s.resize(key_len);
    uint64_t v1 = dist(rng);
    uint64_t v2 = dist(rng);
    std::memcpy(s.data(), &v1, 8);
    std::memcpy(s.data() + 8, &v2, 8);
    keys.push_back(std::move(s));
  }
  return keys;
}

// -----------------------------------------------------------------------------
// Fixture: Boost Flat Set
// -----------------------------------------------------------------------------
class BoostFixture : public benchmark::Fixture {
public:
  boost::unordered_flat_set<std::string> map;
  std::vector<std::string>               keys;
  std::vector<std::string>               miss_keys;

  void SetUp(const benchmark::State& state) override
  {
    size_t num_keys = state.range(0);
    keys            = generate_keys(num_keys);

    // Prepare miss keys
    miss_keys = keys;
    if (!miss_keys.empty()) {
      for (auto& k : miss_keys) {
        k[0] ^= 0xFF;
      }
    }

    // Build Map
    map.clear();
    map.reserve(num_keys);
    for (const auto& k : keys) {
      map.insert(k);
    }
  }

  void TearDown(const benchmark::State&) override
  {
    map.clear();
    keys.clear();
    miss_keys.clear();
  }
};

// -----------------------------------------------------------------------------
// Fixture: Static Index
// -----------------------------------------------------------------------------
class StaticIndexFixture : public benchmark::Fixture {
public:
  vault::containers::static_index index;
  std::vector<std::string>        keys;
  std::vector<std::string>        miss_keys;

  void SetUp(const benchmark::State& state) override
  {
    size_t num_keys = state.range(0);
    keys            = generate_keys(num_keys);

    // Prepare miss keys
    miss_keys = keys;
    if (!miss_keys.empty()) {
      for (auto& k : miss_keys) {
        k[0] ^= 0xFF;
      }
    }

    // Build Index
    index.build(keys);
  }

  void TearDown(const benchmark::State&) override
  {
    // static_index destructor cleans up huge pages automatically
    keys.clear();
    miss_keys.clear();
  }
};

// -----------------------------------------------------------------------------
// Benchmarks
// -----------------------------------------------------------------------------

BENCHMARK_DEFINE_F(BoostFixture, Lookup)(benchmark::State& state)
{
  size_t num_keys = keys.size();
  size_t i        = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(map.contains(keys[i++ & (num_keys - 1)]));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetLabel("Hit Rate: 100%");
}

BENCHMARK_DEFINE_F(BoostFixture, Miss)(benchmark::State& state)
{
  size_t num_keys = keys.size();
  size_t i        = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(map.contains(miss_keys[i++ & (num_keys - 1)]));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetLabel("Hit Rate: 0%");
}

BENCHMARK_DEFINE_F(StaticIndexFixture, Lookup)(benchmark::State& state)
{
  size_t num_keys = keys.size();
  size_t i        = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(index.lookup(keys[i++ & (num_keys - 1)]));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetLabel("Hit Rate: 100%");
}

BENCHMARK_DEFINE_F(StaticIndexFixture, Miss)(benchmark::State& state)
{
  size_t num_keys = keys.size();
  size_t i        = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(index.lookup(miss_keys[i++ & (num_keys - 1)]));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetLabel("Hit Rate: 0%");
}

// -----------------------------------------------------------------------------
// Registration
// -----------------------------------------------------------------------------

BENCHMARK_REGISTER_F(BoostFixture, Lookup)
  ->RangeMultiplier(8)
  ->Range(1 << 16, 1 << 24)
  ->Unit(benchmark::kNanosecond);

BENCHMARK_REGISTER_F(StaticIndexFixture, Lookup)
  ->RangeMultiplier(8)
  ->Range(1 << 16, 1 << 24)
  ->Unit(benchmark::kNanosecond);

BENCHMARK_REGISTER_F(BoostFixture, Miss)
  ->RangeMultiplier(8)
  ->Range(1 << 16, 1 << 24)
  ->Unit(benchmark::kNanosecond);

BENCHMARK_REGISTER_F(StaticIndexFixture, Miss)
  ->RangeMultiplier(8)
  ->Range(1 << 16, 1 << 24)
  ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
