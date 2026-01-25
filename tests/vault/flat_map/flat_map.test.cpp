#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vault/flat_map/eytzinger_layout_policy.hpp>
#include <vault/flat_map/sorted_layout_policy.hpp>
#include <vault/flat_map/implicit_btree_layout_policy.hpp>
#include <vault/flat_map/aliases.hpp> 

#include <vector>
#include <numeric>
#include <algorithm>
#include <random>
#include <stdexcept>

// --- Helper: Test Fixture for Layout Policies ---

template <typename Policy>
void test_layout_permutations(size_t n) {
    if (n == 0) return;

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
        size_t idx = Policy::sorted_rank_to_index(rank, n);
        size_t recovered_rank = Policy::index_to_sorted_rank(idx, n);
        CHECK(recovered_rank == rank);
    }
}

template <typename Policy>
void test_traversal_logic(size_t n) {
    if (n == 0) return;

    size_t start_phys_idx = Policy::sorted_rank_to_index(0, n);
    ptrdiff_t curr = static_cast<ptrdiff_t>(start_phys_idx);

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

template <typename Policy>
void test_lower_bound_correctness(size_t n) {
    if (n == 0) return;

    // Data: 0, 2, 4, ...
    std::vector<int> data(n);
    for (size_t i = 0; i < n; ++i) data[i] = static_cast<int>(i * 2);

    Policy::permute(data);

    // Hit
    for (size_t i = 0; i < n; ++i) {
        int key = static_cast<int>(i * 2);
        auto it = Policy::lower_bound(data, key);
        REQUIRE(it != data.end());
        CHECK(*it == key);
    }

    // Miss (odd numbers) -> should find next even
    for (size_t i = 0; i < n - 1; ++i) {
        int key = static_cast<int>(i * 2 + 1);
        auto it = Policy::lower_bound(data, key);
        REQUIRE(it != data.end());
        CHECK(*it == key + 1);
    }
    
    // Greater than all
    auto it = Policy::lower_bound(data, static_cast<int>(n * 2 + 100));
    CHECK(it == data.end());
}

template <typename Policy>
void test_upper_bound_correctness(size_t n) {
    if (n == 0) return;

    std::vector<int> data(n);
    for (size_t i = 0; i < n; ++i) data[i] = static_cast<int>(i * 2);

    Policy::permute(data);

    // Exact Match -> Should return NEXT element
    for (size_t i = 0; i < n - 1; ++i) {
        int key = static_cast<int>(i * 2); // e.g., 2
        auto it = Policy::upper_bound(data, key);
        REQUIRE(it != data.end());
        CHECK(*it == key + 2); // e.g., 4
    }

    // Last Element -> End
    {
        int key = static_cast<int>((n - 1) * 2);
        auto it = Policy::upper_bound(data, key);
        CHECK(it == data.end());
    }

    // Miss (odd numbers) -> Should return next even (same as lower_bound)
    for (size_t i = 0; i < n - 1; ++i) {
        int key = static_cast<int>(i * 2 + 1);
        auto it = Policy::upper_bound(data, key);
        REQUIRE(it != data.end());
        CHECK(*it == key + 1);
    }
}

// --- High Level Map Interface Tests ---

template <typename MapType>
void test_map_interface(size_t n) {
    if (n == 0) {
        MapType map;
        CHECK(map.empty());
        CHECK(map.size() == 0);
        CHECK(map.begin() == map.end());
        return;
    }

    std::vector<std::pair<int, int>> pairs;
    for (size_t i = 0; i < n; ++i) {
        pairs.emplace_back(static_cast<int>(i * 2), static_cast<int>(i * 2));
    }

    MapType map(pairs.begin(), pairs.end());

    CHECK_FALSE(map.empty());
    CHECK(map.size() == n);

    // 1. Contains / Count
    for (size_t i = 0; i < n; ++i) {
        CHECK(map.contains(static_cast<int>(i * 2)));
        CHECK(map.count(static_cast<int>(i * 2)) == 1);
        CHECK_FALSE(map.contains(static_cast<int>(i * 2 + 1)));
    }

    // 2. At / Find
    for (size_t i = 0; i < n; ++i) {
        int key = static_cast<int>(i * 2);
        CHECK(map.at(key) == key);
        auto it = map.find(key);
        REQUIRE(it != map.end());
        CHECK(it->second == key);
    }

    // 3. Equal Range
    // Existing key (0)
    {
        auto [first, last] = map.equal_range(0);
        REQUIRE(first != map.end());
        CHECK(first->first == 0);
        
        // If n=1, 0 is the max element, so last should be end()
        if (n == 1) {
            CHECK(last == map.end());
        } else {
            CHECK(last != map.end());
            CHECK(last->first == 2); // Next element
        }
        CHECK(std::next(first) == last);
    }
    
    // Missing key (1)
    // Only perform this check if we have enough elements (0, 2...)
    if (n >= 2) {
        auto [first, last] = map.equal_range(1);
        // Should point to 2
        REQUIRE(first != map.end());
        CHECK(first->first == 2); 
        CHECK(first == last);     // Empty range
    } else {
        // If n=1, keys={0}. Search for 1 returns end()
        auto [first, last] = map.equal_range(1);
        CHECK(first == map.end());
        CHECK(last == map.end());
    }

    // 4. Operator[] (Index Access)
    for (size_t i = 0; i < n; ++i) {
        auto val = map[eytzinger::ordered_index<size_t>{i}];
        CHECK(val.first == static_cast<int>(i * 2));
    }
}

// --- Test Definitions ---

using Sorted = eytzinger::sorted_layout_policy;
using Eytzinger = eytzinger::eytzinger_layout_policy<6>;
using ImplicitBTree_Tiny = eytzinger::implicit_btree_layout_policy<2>; 
using ImplicitBTree_Large = eytzinger::implicit_btree_layout_policy<8>; 

TEMPLATE_TEST_CASE("Layout Policy: Index Mapping", "[layout][mapping]", 
                   Sorted, Eytzinger, ImplicitBTree_Tiny, ImplicitBTree_Large) {
    auto n = GENERATE(0, 1, 2, 3, 7, 8, 15, 16, 20, 31, 32, 100);
    test_layout_permutations<TestType>(n);
}

TEMPLATE_TEST_CASE("Layout Policy: Traversal", "[layout][traversal]", 
                   Sorted, Eytzinger, ImplicitBTree_Tiny, ImplicitBTree_Large) {
    auto n = GENERATE(1, 2, 3, 7, 8, 15, 16, 20, 64);
    test_traversal_logic<TestType>(n);
}

TEMPLATE_TEST_CASE("Layout Policy: Lower Bound", "[layout][search]", 
                   Sorted, Eytzinger, ImplicitBTree_Tiny, ImplicitBTree_Large) {
    auto n = GENERATE(0, 1, 2, 5, 8, 15, 16, 100, 1024);
    test_lower_bound_correctness<TestType>(n);
}

TEMPLATE_TEST_CASE("Layout Policy: Upper Bound", "[layout][search]", 
                   Sorted, Eytzinger, ImplicitBTree_Tiny, ImplicitBTree_Large) {
    auto n = GENERATE(0, 1, 2, 5, 8, 15, 16, 100, 1024);
    test_upper_bound_correctness<TestType>(n);
}

// Map Wrappers to Test
using SortedMap = sorted_map<int, int>;
using EytzingerMap = eytzinger_map<int, int>;
using BTreeMap = btree_map<int, int>;

TEMPLATE_TEST_CASE("Layout Map: Interface", "[map][interface]", 
                   SortedMap, EytzingerMap, BTreeMap) {
    auto n = GENERATE(0, 1, 5, 16, 100);
    test_map_interface<TestType>(n);
}
