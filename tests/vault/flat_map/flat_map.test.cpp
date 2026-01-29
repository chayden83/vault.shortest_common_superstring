#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/algorithm/amac.hpp>

#include <vault/flat_map/aliases.hpp>
#include <vault/flat_map/eytzinger_layout_policy.hpp>
#include <vault/flat_map/implicit_btree_layout_policy.hpp>
#include <vault/flat_map/sorted_layout_policy.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

// --- Helper: Policy Naming for Debugging ---
// This ensures test failures tell us WHICH policy failed (e.g., "Sorted K=4")

template <typename T> struct PolicyName;

template <std::size_t K> struct PolicyName<eytzinger::sorted_layout_policy<K>> {
  static std::string name()
  {
    return "Sorted (Arity " + std::to_string(K) + ")";
  }
};

template <std::size_t L>
struct PolicyName<eytzinger::eytzinger_layout_policy<L>> {
  static std::string name()
  {
    return "Eytzinger (L=" + std::to_string(L) + ")";
  }
};

template <std::size_t B>
struct PolicyName<eytzinger::implicit_btree_layout_policy<B>> {
  static std::string name() { return "BTree (B=" + std::to_string(B) + ")"; }
};

// --- Data Generation Utils ---

template <typename T>
std::vector<T> generate_unique_data(size_t n, uint64_t seed = 42)
{
  std::mt19937_64 rng(seed);
  std::set<T>     unique;

  if constexpr (std::is_integral_v<T>) {
    using DistT = std::conditional_t<(sizeof(T) < 2), int16_t, T>;
    auto min_v  = std::numeric_limits<T>::min();
    auto max_v  = std::numeric_limits<T>::max();

    // FIX: Cast to uint64_t to prevent signed overflow (UB) during subtraction
    uint64_t range =
      static_cast<uint64_t>(max_v) - static_cast<uint64_t>(min_v) + 1;
    if (static_cast<uint64_t>(n) > range) {
      n = static_cast<size_t>(range);
    }

    std::uniform_int_distribution<DistT> dist(
      static_cast<DistT>(min_v), static_cast<DistT>(max_v));

    while (unique.size() < n) {
      unique.insert(static_cast<T>(dist(rng)));
    }
  } else if constexpr (std::is_same_v<T, std::string>) {
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    while (unique.size() < n) {
      std::string s;
      s.reserve(16);
      for (int i = 0; i < 16; ++i) {
        s += charset[dist(rng)];
      }
      unique.insert(s);
    }
  }
  return {unique.begin(), unique.end()};
}

// --- Test Driver ---

template <typename MapType> struct MapTestDriver {
  using KeyT   = typename MapType::key_type;
  using ValT   = typename MapType::mapped_type;
  using Policy = typename MapType::policy_type;

  std::vector<KeyT>                  keys;
  std::vector<std::pair<KeyT, ValT>> input;
  MapType                            map;

  explicit MapTestDriver(size_t n)
  {
    // Print Context for debugging failures
    UNSCOPED_INFO("Testing Policy: " << PolicyName<Policy>::name());
    UNSCOPED_INFO("Key Type Size: " << sizeof(KeyT));
    UNSCOPED_INFO("Map Size: " << n);

    keys = generate_unique_data<KeyT>(n);

    input.reserve(keys.size());
    for (const auto& k : keys) {
      if constexpr (std::is_same_v<KeyT, std::string>) {
        input.emplace_back(k, 1);
      } else {
        input.emplace_back(k, static_cast<ValT>(k));
      }
    }

    map = MapType(input.begin(), input.end());
  }

  void verify_basics()
  {
    CHECK(map.size() == keys.size());
    if (keys.empty()) {
      CHECK(map.empty());
    } else {
      CHECK_FALSE(map.empty());
    }
  }

  void verify_iterators_sorted()
  {
    bool is_sorted = std::ranges::equal(
      keys, map, {}, {}, [](auto const& p) { return p.first; });
    CHECK(is_sorted);
  }

  void verify_lookups()
  {
    for (const auto& k : keys) {
      CHECK(map.contains(k));
      auto it = map.find(k);
      REQUIRE(it != map.end());
      CHECK(it->first == k);

      auto lb = map.lower_bound(k);
      CHECK(lb == it);

      auto ub = map.upper_bound(k);
      if (k == keys.back()) {
        CHECK(ub == map.end());
      } else {
        REQUIRE(ub != map.end());
        CHECK(ub->first > k);
      }
    }
  }

  void verify_batch_operations()
  {
    size_t n_needles = keys.size() * 2;
    if (n_needles == 0) {
      n_needles = 10;
    }

    auto needles = generate_unique_data<KeyT>(n_needles, 123);

    // Ensure edge cases (start, end, mid) are present
    if (!keys.empty()) {
      needles.push_back(keys.front());
      needles.push_back(keys.back());
      needles.push_back(keys[keys.size() / 2]);
    }
    std::shuffle(needles.begin(), needles.end(), std::mt19937_64(999));

    using NeedleIter = typename std::vector<KeyT>::iterator;
    using ResultPair = std::pair<NeedleIter, typename MapType::const_iterator>;

    std::vector<ResultPair> results;
    results.reserve(needles.size());

    // 1. Batch Find
    results.clear();
    map.batch_find(
      vault::amac::coordinator<>, needles, std::back_inserter(results));

    CHECK(results.size() == needles.size());
    for (auto [nit, mit] : results) {
      auto expected = map.find(*nit);
      CHECK(mit == expected);
    }

    // 2. Batch Lower Bound
    results.clear();
    map.batch_lower_bound(
      vault::amac::coordinator<>, needles, std::back_inserter(results));

    CHECK(results.size() == needles.size());
    for (auto [nit, mit] : results) {
      auto expected = map.lower_bound(*nit);
      CHECK(mit == expected);
    }

    // 3. Batch Upper Bound
    results.clear();
    map.batch_upper_bound(
      vault::amac::coordinator<>, needles, std::back_inserter(results));

    CHECK(results.size() == needles.size());
    for (auto [nit, mit] : results) {
      auto expected = map.upper_bound(*nit);
      CHECK(mit == expected);
    }
  }
};

// --- Test Cases ---

using SortedBinary  = eytzinger::sorted_layout_policy<2>;
using SortedTernary = eytzinger::sorted_layout_policy<3>;
using SortedK4      = eytzinger::sorted_layout_policy<4>;
using SortedK5      = eytzinger::sorted_layout_policy<5>;
using Eytzinger6    = eytzinger::eytzinger_layout_policy<6>;
using BTree8        = eytzinger::implicit_btree_layout_policy<8>;

TEMPLATE_TEST_CASE("Layout Map: Correctness",
  "[map][k-ary]",
  (eytzinger::layout_map<int, int, std::less<int>, SortedBinary>),
  (eytzinger::layout_map<int, int, std::less<int>, SortedTernary>),
  (eytzinger::layout_map<int, int, std::less<int>, SortedK4>),
  (eytzinger::layout_map<int, int, std::less<int>, SortedK5>),
  (eytzinger::layout_map<int, int, std::less<int>, Eytzinger6>),
  (eytzinger::layout_map<int, int, std::less<int>, BTree8>))
{
  size_t                  n = GENERATE(0, 1, 2, 5, 16, 31, 64, 100);
  MapTestDriver<TestType> driver(n);

  SECTION("Invariants")
  {
    driver.verify_basics();
    driver.verify_iterators_sorted();
  }
  SECTION("Single Lookup") { driver.verify_lookups(); }
  SECTION("Batch Operations") { driver.verify_batch_operations(); }
}

TEMPLATE_TEST_CASE("Layout Map: Key Types",
  "[map][types]",
  int8_t,
  uint16_t,
  int32_t,
  uint64_t,
  std::string)
{
  using Policy = eytzinger::sorted_layout_policy<4>;
  using MapType =
    eytzinger::layout_map<TestType, int, std::less<TestType>, Policy>;

  size_t                 n = GENERATE(0, 16, 64);
  MapTestDriver<MapType> driver(n);

  driver.verify_lookups();
  driver.verify_batch_operations();
}

// Container Compatibility
TEMPLATE_TEST_CASE("Layout Map: Deque Storage",
  "[map][deque]",
  (eytzinger::layout_map<int,
    int,
    std::less<int>,
    eytzinger::sorted_layout_policy<4>,
    std::allocator<std::pair<const int, int>>,
    std::deque>),
  (eytzinger::layout_map<int,
    int,
    std::less<int>,
    eytzinger::implicit_btree_layout_policy<4>,
    std::allocator<std::pair<const int, int>>,
    std::vector,
    std::deque>))
{
  size_t                  n = GENERATE(16, 64);
  MapTestDriver<TestType> driver(n);
  driver.verify_lookups();
}

TEST_CASE("Sorted Layout: Topology Identity", "[layout][topology]")
{
  using P_K2 = eytzinger::sorted_layout_policy<2>;
  using P_K5 = eytzinger::sorted_layout_policy<5>;
  size_t n   = 20;
  for (size_t i = 0; i < n; ++i) {
    CHECK(P_K2::sorted_rank_to_index(i, n) == i);
    CHECK(P_K5::sorted_rank_to_index(i, n) == i);
  }
}
