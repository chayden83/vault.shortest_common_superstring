#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
#include <utility>
#include <vector>

// --- Helper: Random Data Generation ---

template <typename T> struct RandomDataGenerator {
  static std::vector<T> generate_sorted_unique(size_t n)
  {
    std::mt19937_64 rng(42); // Fixed seed for reproducibility

    // uniform_int_distribution does not support char types directly
    using DistType = std::conditional_t<sizeof(T) == 1, int16_t, T>;

    DistType min_val = static_cast<DistType>(std::numeric_limits<T>::min());
    DistType max_val = static_cast<DistType>(std::numeric_limits<T>::max());

    // Safety: If n exceeds the range of T (e.g. n=300 for uint8_t), cap it.
    size_t range_size = static_cast<size_t>(max_val - min_val) + 1;
    if (n > range_size) {
      n = range_size;
    }

    std::uniform_int_distribution<DistType> dist(min_val, max_val);

    // Use a set to ensure uniqueness efficiently during generation
    std::set<T> unique_values;
    while (unique_values.size() < n) {
      unique_values.insert(static_cast<T>(dist(rng)));
    }

    return std::vector<T>(unique_values.begin(), unique_values.end());
  }
};

// Specialization for std::string
template <> struct RandomDataGenerator<std::string> {
  static std::vector<std::string> generate_sorted_unique(size_t n)
  {
    std::mt19937_64 rng(42);
    // Characters to use in random strings (alphanumeric)
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);

    std::set<std::string> unique_values;
    while (unique_values.size() < n) {
      std::string s;
      s.reserve(32);
      for (int i = 0; i < 32; ++i) {
        s += charset[dist(rng)];
      }
      unique_values.insert(std::move(s));
    }
    return std::vector<std::string>(unique_values.begin(), unique_values.end());
  }
};

// --- Test Fixture: Layout Topology (Type Independent) ---

template <typename Policy> void test_layout_permutations(size_t n)
{
  if (n == 0) {
    return;
  }

  std::vector<size_t> p_indices;
  p_indices.reserve(n);

  // 1. Verify sorted_rank_to_index generates a valid permutation of 0..N-1
  for (size_t rank = 0; rank < n; ++rank) {
    size_t idx = Policy::sorted_rank_to_index(rank, n);
    REQUIRE(idx < n);
    p_indices.push_back(idx);
  }

  // Check uniqueness
  std::sort(p_indices.begin(), p_indices.end());
  auto last = std::unique(p_indices.begin(), p_indices.end());
  REQUIRE(std::distance(p_indices.begin(), last) == static_cast<ptrdiff_t>(n));

  // 2. Verify Round-Trip (Index -> Rank -> Index)
  for (size_t rank = 0; rank < n; ++rank) {
    size_t idx            = Policy::sorted_rank_to_index(rank, n);
    size_t recovered_rank = Policy::index_to_sorted_rank(idx, n);
    CHECK(recovered_rank == rank);
  }
}

template <typename Policy> void test_traversal_logic(size_t n)
{
  if (n == 0) {
    return;
  }

  size_t    start_phys_idx = Policy::sorted_rank_to_index(0, n);
  ptrdiff_t curr           = static_cast<ptrdiff_t>(start_phys_idx);

  // 1. Forward Traversal
  for (size_t rank = 0; rank < n - 1; ++rank) {
    size_t expected_curr = Policy::sorted_rank_to_index(rank, n);
    CHECK(static_cast<size_t>(curr) == expected_curr);

    ptrdiff_t next = Policy::next_index(curr, n);
    CHECK(next != -1);

    size_t expected_next = Policy::sorted_rank_to_index(rank + 1, n);
    CHECK(static_cast<size_t>(next) == expected_next);

    curr = next;
  }
  CHECK(Policy::next_index(curr, n) == -1);

  // 2. Backward Traversal
  curr = static_cast<ptrdiff_t>(Policy::sorted_rank_to_index(n - 1, n));

  for (size_t rank = n - 1; rank > 0; --rank) {
    size_t expected_curr = Policy::sorted_rank_to_index(rank, n);
    CHECK(static_cast<size_t>(curr) == expected_curr);

    ptrdiff_t prev = Policy::prev_index(curr, n);
    CHECK(prev != -1);

    size_t expected_prev = Policy::sorted_rank_to_index(rank - 1, n);
    CHECK(static_cast<size_t>(prev) == expected_prev);

    curr = prev;
  }
  CHECK(Policy::prev_index(curr, n) == -1);
}

// --- Topology Tests ---

using Sorted              = eytzinger::sorted_layout_policy;
using Eytzinger           = eytzinger::eytzinger_layout_policy<6>;
using ImplicitBTree_Tiny  = eytzinger::implicit_btree_layout_policy<2>;
using ImplicitBTree_Large = eytzinger::implicit_btree_layout_policy<8>;

TEMPLATE_TEST_CASE(
    "Layout Policy: Index Mapping",
    "[layout][mapping]",
    Sorted,
    Eytzinger,
    ImplicitBTree_Tiny,
    ImplicitBTree_Large
)
{
  auto n = GENERATE(0, 1, 2, 3, 7, 8, 15, 16, 20, 31, 32, 100);
  test_layout_permutations<TestType>(n);
}

TEMPLATE_TEST_CASE(
    "Layout Policy: Traversal",
    "[layout][traversal]",
    Sorted,
    Eytzinger,
    ImplicitBTree_Tiny,
    ImplicitBTree_Large
)
{
  auto n = GENERATE(1, 2, 3, 7, 8, 15, 16, 20, 64);
  test_traversal_logic<TestType>(n);
}

// --- Map Interface Tests with Random Types ---

template <typename MapType> void test_map_random(size_t n)
{
  using KeyT = typename MapType::key_type;
  using ValT = typename MapType::mapped_type;

  // 1. Generate Input
  std::vector<KeyT> keys = RandomDataGenerator<KeyT>::generate_sorted_unique(n);
  // Actual N might be smaller if type range is small (e.g. uint8_t with n=300)
  n = keys.size();

  if (n == 0) {
    MapType map;
    CHECK(map.empty());
    return;
  }

  // Create pairs for map construction
  std::vector<std::pair<KeyT, ValT>> input;
  input.reserve(n);
  for (const auto& k : keys) {
    // For string, static_cast doesn't work directly to int, so use
    // construct/convert
    if constexpr (std::is_same_v<KeyT, std::string>) {
      input.emplace_back(k, 1); // Dummy value
    } else {
      input.emplace_back(k, static_cast<ValT>(k));
    }
  }

  // 2. Construct Map
  MapType map(input.begin(), input.end());

  CHECK_FALSE(map.empty());
  CHECK(map.size() == n);

  // 3. Verify Lookups (Existing Keys)
  for (const auto& k : keys) {
    // Contains
    CHECK(map.contains(k));
    CHECK(map.count(k) == 1);

    // Find
    auto it = map.find(k);
    REQUIRE(it != map.end());
    CHECK(it->first == k);
    if constexpr (!std::is_same_v<KeyT, std::string>) {
      CHECK(it->second == static_cast<ValT>(k));
    }

    // At
    if constexpr (!std::is_same_v<KeyT, std::string>) {
      CHECK(map.at(k) == static_cast<ValT>(k));
    }

    // Lower Bound
    auto lb = map.lower_bound(k);
    REQUIRE(lb != map.end());
    CHECK(lb->first == k);

    // Upper Bound
    auto ub = map.upper_bound(k);
    // UB should be the element strictly greater than k
    // Since 'keys' is sorted, this is the next element in the vector
    if (k == keys.back()) {
      CHECK(ub == map.end());
    } else {
      REQUIRE(ub != map.end());
      CHECK(ub->first > k);
    }
  }

  // 4. Verify Lookups (Missing Keys) We try to find values "between"
  // our random keys to test misses. (Skipped for strings for simplicity)
  if constexpr (!std::is_same_v<KeyT, std::string>) {
    if (n > 1) {
      for (size_t i = 0; i < n - 1; ++i) {
        // Try to find a midpoint.  Note: for integers, midpoint might
        // equal keys[i] if difference is 1.  We only test if there is a
        // gap.
        if (keys[i + 1] > keys[i] + 1) {
          KeyT missing = static_cast<KeyT>(keys[i] + 1);

          CHECK_FALSE(map.contains(missing));
          CHECK(map.find(missing) == map.end());

          // LB of missing should be keys[i+1]
          auto lb = map.lower_bound(missing);
          REQUIRE(lb != map.end());
          CHECK(lb->first == keys[i + 1]);
        }
      }
    }
  }

  // 5. Verify that iterating the layout map produces the keys in
  // sorted order.
  CHECK(
      std::ranges::equal(
          keys,
          map,
          {},
          {},
          [](auto const& p) -> decltype(auto) { return p.first; }
      )
  );
}

// --- Test Case Generation ---

// Helper macro to define all 3 layouts for a specific key type
#define LAYOUT_MAPS_FOR_TYPE(KeyType)                                          \
  (eytzinger::sorted_map<KeyType, int>),                                       \
      (eytzinger::eytzinger_map<KeyType, int>),                                \
      (eytzinger::btree_map<KeyType, int>)

// Test all layouts with all requested integer types
TEMPLATE_TEST_CASE(
    "Layout Map: Random Keys Integration",
    "[map][random]",
    LAYOUT_MAPS_FOR_TYPE(int8_t),
    LAYOUT_MAPS_FOR_TYPE(uint8_t),
    LAYOUT_MAPS_FOR_TYPE(int16_t),
    LAYOUT_MAPS_FOR_TYPE(uint16_t),
    LAYOUT_MAPS_FOR_TYPE(int32_t),
    LAYOUT_MAPS_FOR_TYPE(uint32_t),
    LAYOUT_MAPS_FOR_TYPE(int64_t),
    LAYOUT_MAPS_FOR_TYPE(uint64_t)
)
{
  // Test with various sizes, including edge cases
  auto n = GENERATE(0, 1, 16, 64, 100);
  test_map_random<TestType>(n);
}

// Separate Test Case for Strings (larger sizes)
TEMPLATE_TEST_CASE(
    "Layout Map: Random String Keys",
    "[map][string]",
    LAYOUT_MAPS_FOR_TYPE(std::string)
)
{
  // Test with various sizes
  auto n = GENERATE(0, 1, 16, 64, 100);
  test_map_random<TestType>(n);
}

// --- Deque Compatibility Tests ---

// 1. Fully Deque-based Map (Sorted Layout Only)
template <typename K, typename V>
using DequeSortedMap = eytzinger::layout_map<
    K,
    V,
    std::less<K>,
    eytzinger::sorted_layout_policy,
    std::allocator<std::pair<const K, V>>,
    std::deque, // Keys: Deque
    std::deque  // Values: Deque
    >;

// 2. Hybrid Map: Vector Keys (Contiguous) + Deque Values (Non-contiguous)
// This is valid for Eytzinger and BTree because they only search Keys.
template <typename K, typename V>
using DequeValueEytzingerMap = eytzinger::layout_map<
    K,
    V,
    std::less<K>,
    eytzinger::eytzinger_layout_policy<6>,
    std::allocator<std::pair<const K, V>>,
    std::vector, // Keys: MUST be Vector for Eytzinger
    std::deque   // Values: Can be Deque
    >;

template <typename K, typename V>
using DequeValueBTreeMap = eytzinger::layout_map<
    K,
    V,
    std::less<K>,
    eytzinger::implicit_btree_layout_policy<8>,
    std::allocator<std::pair<const K, V>>,
    std::vector, // Keys: MUST be Vector for BTree
    std::deque   // Values: Can be Deque
    >;

TEMPLATE_TEST_CASE(
    "Layout Map: Deque Storage Configurations",
    "[map][deque]",
    (DequeSortedMap<int, int>),
    (DequeValueEytzingerMap<int, int>),
    (DequeValueBTreeMap<int, int>)
)
{
  auto n = GENERATE(0, 1, 16, 100);
  test_map_random<TestType>(n);
}

// --- Concept Guardrail Tests ---

// Helper to check if a specific configuration is valid
template <
    typename K,
    typename V,
    typename Policy,
    template <typename, typename> typename KeyCont>
concept CanInstantiateMap = requires {
  typename eytzinger::layout_map<
      K,
      V,
      std::less<K>,
      Policy,
      std::allocator<std::pair<const K, V>>,
      KeyCont>;
};

TEST_CASE(
    "Layout Policy: Container Compatibility Enforcement", "[layout][concepts]"
)
{
  using Eytzinger = eytzinger::eytzinger_layout_policy<6>;
  using BTree     = eytzinger::implicit_btree_layout_policy<8>;
  using Sorted    = eytzinger::sorted_layout_policy;

  // 1. Vector (Contiguous) should work for ALL policies
  CHECK(CanInstantiateMap<int, int, Eytzinger, std::vector>);
  CHECK(CanInstantiateMap<int, int, BTree, std::vector>);
  CHECK(CanInstantiateMap<int, int, Sorted, std::vector>);

  // 2. Deque (Random Access, NOT Contiguous)
  // Should FAIL for Eytzinger/BTree (if your trait requires
  // contiguous_iterator)
  CHECK_FALSE(CanInstantiateMap<int, int, Eytzinger, std::deque>);
  CHECK_FALSE(CanInstantiateMap<int, int, BTree, std::deque>);

  // Should PASS for Sorted (std::ranges::lower_bound works on random access
  // iterators)
  CHECK(CanInstantiateMap<int, int, Sorted, std::deque>);
}
