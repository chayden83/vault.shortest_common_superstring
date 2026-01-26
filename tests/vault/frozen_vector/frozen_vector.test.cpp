#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// Include your library headers
#include <vault/frozen_vector/frozen_vector.hpp>
#include <vault/frozen_vector/frozen_vector_builder.hpp>
#include <vault/frozen_vector/shared_storage_policy.hpp>

using namespace frozen;

// ============================================================================
// HELPER POLICIES
// ============================================================================

// 1. Copyable Policy for Testing Deep Copy
template <typename T> struct copyable_policy {
  using mutable_handle_type = std::shared_ptr<T[]>;

  template <typename Alloc>
  static mutable_handle_type allocate(size_t n, const Alloc &a)
  {
    if (n == 0)
      return nullptr;
    return std::allocate_shared_for_overwrite<T[]>(a, n);
  }

  template <typename Alloc>
  static mutable_handle_type
  copy(const mutable_handle_type &src, size_t n, const Alloc &a)
  {
    if (!src || n == 0)
      return nullptr;
    auto new_data = allocate(n, a);
    std::copy(src.get(), src.get() + n, new_data.get());
    return new_data;
  }
};

// 2. Local Unique Storage Policy
struct test_unique_policy {
  using mutable_handle_type = std::unique_ptr<int[]>;

  template <typename Alloc>
  static mutable_handle_type allocate(size_t n, const Alloc &)
  {
    return std::make_unique_for_overwrite<int[]>(n);
  }
};

// ============================================================================
// TEST CASES
// ============================================================================

TEST_CASE("frozen_vector_builder: Basic Construction and Mutation", "[builder]")
{
  SECTION("Default construction")
  {
    frozen_vector_builder<int> builder;
    REQUIRE(builder.empty());
    REQUIRE(builder.size() == 0);
    REQUIRE(builder.capacity() == 0);
  }

  SECTION("Sized construction")
  {
    frozen_vector_builder<int> builder(10);
    REQUIRE(builder.size() == 10);
    REQUIRE(builder.capacity() == 10);

    // CORRECTION: Since we use allocate_for_overwrite, values are
    // indeterminate. We cannot assert they are 0. Instead, we verify we can
    // write to them.
    builder[0] = 42;
    builder[9] = 100;

    REQUIRE(builder[0] == 42);
    REQUIRE(builder[9] == 100);
  }

  SECTION("push_back and resizing")
  {
    frozen_vector_builder<std::string> builder;

    builder.push_back("Hello");
    builder.push_back("World");

    REQUIRE(builder.size() == 2);
    REQUIRE(builder.capacity() >= 2);
    REQUIRE(builder[0] == "Hello");
    REQUIRE(builder[1] == "World");

    builder.emplace_back("!");
    REQUIRE(builder.size() == 3);
    REQUIRE(builder.back() == "!");
  }

  SECTION("reserve increases capacity")
  {
    frozen_vector_builder<int> builder;
    builder.reserve(100);
    REQUIRE(builder.capacity() >= 100);
    REQUIRE(builder.size() == 0);
  }

  SECTION("clear resets size but keeps capacity")
  {
    frozen_vector_builder<int> builder(50);
    size_t cap = builder.capacity();

    builder.clear();
    REQUIRE(builder.size() == 0);
    REQUIRE(builder.capacity() == cap);
  }

  SECTION("shrink_to_fit reduces capacity")
  {
    frozen_vector_builder<int> builder;
    builder.reserve(100);
    builder.push_back(1);
    builder.push_back(2);

    REQUIRE(builder.capacity() >= 100);
    builder.shrink_to_fit();
    REQUIRE(builder.capacity() == 2);
    REQUIRE(builder.size() == 2);
  }
}

TEST_CASE("frozen_vector_builder: Access and Range Support", "[builder]")
{
  frozen_vector_builder<int> builder;
  builder.push_back(10);
  builder.push_back(20);
  builder.push_back(30);

  SECTION("Element access")
  {
    REQUIRE(builder[0] == 10);
    REQUIRE(builder.at(1) == 20);
    REQUIRE_THROWS_AS(builder.at(99), std::out_of_range);
    REQUIRE(builder.front() == 10);
    REQUIRE(builder.back() == 30);
  }

  SECTION("Iterators")
  {
    std::vector<int> result;
    for (auto val : builder) {
      result.push_back(val);
    }
    REQUIRE(result == std::vector<int>{10, 20, 30});
  }

  SECTION("Reverse Iterators")
  {
    auto it = builder.rbegin();
    REQUIRE(*it == 30);
    ++it;
    REQUIRE(*it == 20);
  }

  SECTION("append_range (C++20)")
  {
    std::vector<int> more_data = {40, 50};
    builder.append_range(more_data);

    REQUIRE(builder.size() == 5);
    REQUIRE(builder[3] == 40);
    REQUIRE(builder[4] == 50);
  }
}

TEST_CASE("Freezing Behavior", "[freeze]")
{
  frozen_vector_builder<std::string> builder;
  builder.push_back("A");
  builder.push_back("B");

  SECTION("Standard freeze returns frozen_vector")
  {
    auto frozen = std::move(builder).freeze();

    REQUIRE(frozen.size() == 2);
    REQUIRE(frozen[0] == "A");
    REQUIRE(frozen[1] == "B");

    REQUIRE(builder.size() == 0);
    REQUIRE(builder.capacity() == 0);
    REQUIRE(builder.begin() == nullptr);
  }

  SECTION("Implicit trait lookup (shared_ptr -> shared_ptr)")
  {
    auto frozen =
        std::move(builder).freeze<std::shared_ptr<const std::string[]>>();
    REQUIRE(frozen.size() == 2);
  }
}

TEST_CASE("frozen_vector: Immutability and Sharing", "[frozen_vector]")
{
  frozen_vector_builder<int> builder;
  builder.push_back(1);
  builder.push_back(2);
  auto vec1 = std::move(builder).freeze();

  SECTION("Copying is shallow (O(1))")
  {
    auto vec2 = vec1;

    REQUIRE(vec1.size() == 2);
    REQUIRE(vec2.size() == 2);
    REQUIRE(vec1.data() == vec2.data());
  }

  SECTION("Assignment is shallow")
  {
    frozen_vector<int> vec3;
    vec3 = vec1;
    REQUIRE(vec3.data() == vec1.data());
  }

  SECTION("Move semantics")
  {
    const int *original_ptr = vec1.data();
    auto vec_moved = std::move(vec1);

    REQUIRE(vec_moved.data() == original_ptr);
    REQUIRE(vec1.size() == 0);
    REQUIRE(vec1.data() == nullptr);
  }

  SECTION("Comparison Operators")
  {
    auto vec_copy = vec1;
    REQUIRE(vec1 == vec_copy);

    frozen_vector_builder<int> other_builder;
    other_builder.push_back(1);
    other_builder.push_back(99);
    auto vec_diff = std::move(other_builder).freeze();

    REQUIRE(vec1 != vec_diff);
    REQUIRE(vec1 < vec_diff);
  }
}

TEST_CASE("Policy: Non-Copyable vs Copyable", "[policy]")
{
  SECTION("Default Policy is Non-Copyable")
  {
    using DefBuilder = frozen_vector_builder<int>;
    static_assert(
        !std::is_copy_constructible_v<DefBuilder>,
        "Default builder should NOT be copy constructible"
    );
    static_assert(
        !std::is_copy_assignable_v<DefBuilder>,
        "Default builder should NOT be copy assignable"
    );
    static_assert(
        std::is_move_constructible_v<DefBuilder>,
        "Default builder MUST be move constructible"
    );
  }

  SECTION("Custom Policy enables Deep Copy")
  {
    using CopyBuilder = frozen_vector_builder<int, copyable_policy<int>>;

    static_assert(
        std::is_copy_constructible_v<CopyBuilder>,
        "Policy with copy() should enable copy constructor"
    );

    CopyBuilder b1;
    b1.push_back(100);
    b1.push_back(200);

    CopyBuilder b2 = b1;

    REQUIRE(b2.size() == 2);
    REQUIRE(b2[0] == 100);

    b2[0] = 999;
    REQUIRE(b1[0] == 100);
    REQUIRE(b2[0] == 999);

    CopyBuilder b3;
    b3 = b1;
    REQUIRE(b3.size() == 2);
    REQUIRE(b3[0] == 100);
  }
}

TEST_CASE("frozen_vector_builder: Allocator Awareness", "[allocator]")
{
  frozen_vector_builder<int> builder;
  builder.push_back(42);

  auto alloc = builder.get_allocator();
  frozen_vector_builder<int> builder2(alloc);

  builder2 = std::move(builder);
  REQUIRE(builder2.size() == 1);
  REQUIRE(builder2[0] == 42);
}

TEST_CASE("Custom Freeze Traits (std::unique_ptr source)", "[traits]")
{
  // Uses global 'test_unique_policy'
  frozen_vector_builder<int, test_unique_policy> builder;

  builder.push_back(1);
  builder.push_back(2);

  auto frozen = std::move(builder).freeze();

  REQUIRE(frozen.size() == 2);
  REQUIRE(frozen[0] == 1);
  REQUIRE(frozen[1] == 2);

  auto frozen_copy = frozen;
  REQUIRE(frozen_copy.data() == frozen.data());
}
