#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <algorithm>
#include <memory>
#include <string>

// Assuming the class is defined in this header
#include <vault/segmented_vector/segmented_vector.hpp>

// -----------------------------------------------------------------------------
// Helper Types for Testing
// -----------------------------------------------------------------------------

// A type that counts how many times it is constructed/destroyed to verify
// cleanup
struct LifecycleTracker {
  static int construction_count;
  static int destruction_count;
  int value;

  LifecycleTracker(int v)
      : value(v)
  {
    construction_count++;
  }
  LifecycleTracker(const LifecycleTracker &o)
      : value(o.value)
  {
    construction_count++;
  }
  LifecycleTracker(LifecycleTracker &&o) noexcept
      : value(o.value)
  {
    construction_count++;
  }
  ~LifecycleTracker()
  {
    destruction_count++;
  }

  static void reset()
  {
    construction_count = 0;
    destruction_count = 0;
  }
};
int LifecycleTracker::construction_count = 0;
int LifecycleTracker::destruction_count = 0;

// A type that throws an exception during copy construction
struct ThrowingType {
  int value;
  static bool should_throw;

  ThrowingType(int v)
      : value(v)
  {}
  ThrowingType(const ThrowingType &other)
      : value(other.value)
  {
    if (should_throw) {
      throw std::runtime_error("Simulated Copy Failure");
    }
  }
};
bool ThrowingType::should_throw = false;

// -----------------------------------------------------------------------------
// Test Cases
// -----------------------------------------------------------------------------

TEST_CASE("Basic Container Operations", "[basics]")
{
  segmented_vector<int> sv;

  SECTION("Initially empty")
  {
    REQUIRE(sv.empty());
    REQUIRE(sv.size() == 0);
    REQUIRE(sv.capacity() == 0);
  }

  SECTION("Push back and element access")
  {
    sv.push_back(10);
    sv.push_back(20);
    sv.push_back(30);

    REQUIRE_FALSE(sv.empty());
    REQUIRE(sv.size() == 3);

    CHECK(sv[0] == 10);
    CHECK(sv[1] == 20);
    CHECK(sv[2] == 30);

    CHECK(sv.front() == 10);
    CHECK(sv.back() == 30);
  }

  SECTION("Clear resets size but maintains capacity")
  {
    for (int i = 0; i < 100; ++i)
      sv.push_back(i);
    std::size_t old_cap = sv.capacity();

    sv.clear();
    REQUIRE(sv.empty());
    REQUIRE(sv.size() == 0);
    REQUIRE(sv.capacity() == old_cap); // Capacity should be preserved
  }

  SECTION("Bounds checking with at()")
  {
    sv.push_back(1);
    REQUIRE_THROWS_AS(sv.at(1), std::out_of_range);
    REQUIRE_NOTHROW(sv.at(0));
  }
}

TEST_CASE("Growth Strategy Verification", "[growth]")
{
  // Requirements:
  // Initial Cap (template param) = 8 (default).
  // Block 0: Size 8. Total Cap: 8.
  // Block 1: Size 8. Total Cap: 16.
  // Block 2: Size 16. Total Cap: 32.
  // Block 3: Size 32. Total Cap: 64.

  segmented_vector<int, std::allocator<int>, std::integral_constant<size_t, 8>>
      sv;

  // 1. Push 8 elements (Fill Block 0)
  // Indices: 0-7
  for (int i = 0; i < 8; ++i)
    sv.push_back(i);
  REQUIRE(sv.capacity() == 8);

  // 2. Push 9th element (Allocates Block 1, size 8)
  // Index: 8
  sv.push_back(8);
  REQUIRE(sv.capacity() == 16); // 8 + 8

  // 3. Push until 16 elements (Fill Block 1)
  // Previous count: 9. Need to reach 16.
  // Indices: 9-15 (7 elements)
  // FIX: Loop starts at 9, not 10.
  for (int i = 9; i < 16; ++i)
    sv.push_back(i);
  REQUIRE(sv.size() == 16);
  REQUIRE(sv.capacity() == 16);

  // 4. Push 17th element (Allocates Block 2, size 16)
  // Index: 16
  // Size becomes 17. Capacity should double to 32.
  sv.push_back(16);
  REQUIRE(sv.capacity() == 32); // 16 + 16

  // 5. Push until 32 elements (Fill Block 2)
  for (int i = 17; i < 32; ++i)
    sv.push_back(i);
  REQUIRE(sv.capacity() == 32);

  // 6. Push 33rd element (Allocates Block 3, size 32)
  sv.push_back(32);
  REQUIRE(sv.capacity() == 64); // 32 + 32
}

TEST_CASE("Reference Stability", "[stability]")
{
  segmented_vector<int> sv;

  // Fill first block
  sv.push_back(100);
  int *ptr_to_first = &sv[0];

  // Force multiple reallocations / new blocks
  for (int i = 0; i < 1000; ++i) {
    sv.push_back(i);
  }

  // Check that the address of the first element hasn't changed
  REQUIRE(&sv[0] == ptr_to_first);
  REQUIRE(*ptr_to_first == 100);
}

TEST_CASE("Iterator Compliance", "[iterator]")
{
  segmented_vector<int> sv;
  for (int i = 0; i < 100; ++i)
    sv.push_back(i);

  SECTION("Random Access Traits")
  {
    using Iter = decltype(sv.begin());
    STATIC_REQUIRE(std::random_access_iterator<Iter>);
    STATIC_REQUIRE(
        std::is_same_v<
            std::iterator_traits<Iter>::iterator_category,
            std::random_access_iterator_tag>
    );
  }

  SECTION("Traversal")
  {
    int expected = 0;
    for (auto val : sv) {
      CHECK(val == expected++);
    }
  }

  SECTION("Random Access Arithmetic")
  {
    auto it = sv.begin();
    CHECK(*(it + 50) == 50);
    CHECK((it + 50) - it == 50);

    auto end_it = sv.end();
    CHECK(*(end_it - 1) == 99);
    CHECK(end_it - sv.begin() == 100);
  }

  SECTION("Standard Algorithms")
  {
    // Sort (requires random access and swapping)
    std::ranges::reverse(sv);
    CHECK(sv.front() == 99);
    CHECK(sv.back() == 0);

    std::ranges::sort(sv);
    CHECK(sv.front() == 0);
    CHECK(sv.back() == 99);

    // Binary search
    bool found = std::binary_search(sv.begin(), sv.end(), 50);
    CHECK(found);
  }
}

TEST_CASE("Object Semantics (Copy/Move)", "[semantics]")
{
  segmented_vector<std::string> original;
  original.push_back("Hello");
  original.push_back("World");

  SECTION("Copy Constructor")
  {
    segmented_vector<std::string> copy = original;
    REQUIRE(copy.size() == 2);
    REQUIRE(copy[0] == "Hello");

    // Deep copy check
    copy[0] = "Modified";
    CHECK(original[0] == "Hello");
  }

  SECTION("Move Constructor")
  {
    std::string *original_ptr = &original[0];
    segmented_vector<std::string> moved = std::move(original);

    REQUIRE(moved.size() == 2);
    REQUIRE(original.size() == 0); // Moved-from state

    // Pointer stability check: Moving the container should steal the blocks
    REQUIRE(&moved[0] == original_ptr);
  }

  SECTION("Copy Assignment")
  {
    segmented_vector<std::string> copy;
    copy.push_back("Garbage");
    copy = original;

    REQUIRE(copy.size() == 2);
    REQUIRE(copy[0] == "Hello");
  }

  SECTION("Move Assignment")
  {
    segmented_vector<std::string> moved;
    moved = std::move(original);
    REQUIRE(moved.size() == 2);
    REQUIRE(moved[0] == "Hello");
    REQUIRE(original.size() == 0);
  }
}

TEST_CASE("Exception Safety (Strong Guarantee)", "[exception]")
{
  segmented_vector<ThrowingType> sv;

  // Fill with some valid data
  sv.emplace_back(10);
  sv.emplace_back(20);
  size_t initial_size = sv.size();
  size_t initial_cap = sv.capacity();

  ThrowingType::should_throw = true;

  SECTION("Push_back throwing")
  {
    ThrowingType val(30);
    REQUIRE_THROWS_AS(sv.push_back(val), std::runtime_error);

    // Strong guarantee: state unchanged
    REQUIRE(sv.size() == initial_size);
    // Note: Capacity might have increased if the throw happened *after*
    // allocation but *during* construction. However, the vector must remain
    // valid.
    CHECK(sv[0].value == 10);
    CHECK(sv[1].value == 20);
  }

  ThrowingType::should_throw = false;
}

TEST_CASE("Destruction and Cleanup", "[memory]")
{
  LifecycleTracker::reset();

  {
    segmented_vector<LifecycleTracker> sv;
    sv.emplace_back(1);
    sv.emplace_back(2);
    sv.emplace_back(3);
  } // sv goes out of scope

  REQUIRE(LifecycleTracker::construction_count == 3);
  REQUIRE(LifecycleTracker::destruction_count == 3);
}

// -----------------------------------------------------------------------------
// Allocator Awareness Test
// -----------------------------------------------------------------------------

template <typename T> struct TrackingAllocator {
  using value_type = T;

  static int allocations;
  static int deallocations;

  TrackingAllocator() = default;
  template <typename U> TrackingAllocator(const TrackingAllocator<U> &)
  {}

  T *allocate(std::size_t n)
  {
    allocations++;
    return std::allocator<T>().allocate(n);
  }

  void deallocate(T *p, std::size_t n)
  {
    deallocations++;
    std::allocator<T>().deallocate(p, n);
  }

  friend bool operator==(const TrackingAllocator &, const TrackingAllocator &)
  {
    return true;
  }
  friend bool operator!=(const TrackingAllocator &, const TrackingAllocator &)
  {
    return false;
  }
};

template <typename T> int TrackingAllocator<T>::allocations = 0;
template <typename T> int TrackingAllocator<T>::deallocations = 0;

TEST_CASE("Allocator Awareness", "[allocator]")
{
  using Alloc = TrackingAllocator<int>;
  Alloc::allocations = 0;
  Alloc::deallocations = 0;

  {
    segmented_vector<int, Alloc, std::integral_constant<size_t, 2>> sv;

    // Cap 2 (Alloc #1)
    sv.push_back(1);
    sv.push_back(2);

    // Cap 2+2=4 (Alloc #2)
    sv.push_back(3);

    REQUIRE(Alloc::allocations == 2);
  }
  // Destructor should deallocate all blocks
  REQUIRE(Alloc::deallocations == 2);
}

TEST_CASE("Large Scale / Block Boundary Stress", "[stress]")
{
  // Uses small block size to force many block transitions
  segmented_vector<
      size_t,
      std::allocator<size_t>,
      std::integral_constant<size_t, 4>>
      sv;

  constexpr size_t N = 10000;

  for (size_t i = 0; i < N; ++i) {
    sv.push_back(i);
  }

  REQUIRE(sv.size() == N);

  // Verify every single value
  // This catches off-by-one errors in the bitwise indexing logic
  for (size_t i = 0; i < N; ++i) {
    if (sv[i] != i) {
      FAIL(
          "Mismatch at index " << i << ": expected " << i << ", got " << sv[i]
      );
    }
  }
}
