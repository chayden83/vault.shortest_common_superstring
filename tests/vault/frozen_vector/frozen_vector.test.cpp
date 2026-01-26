#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <vault/frozen_vector/frozen_vector.hpp>
#include <vault/frozen_vector/frozen_vector_builder.hpp>
#include <vault/frozen_vector/local_shared_storage_policy.hpp>
#include <vault/frozen_vector/shared_storage_policy.hpp>
#include <vault/frozen_vector/unique_storage_policy.hpp>

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

    // Zero-initialization check
    REQUIRE(builder[0] == 0);
    REQUIRE(builder[9] == 0);
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
}

TEST_CASE("Storage Policy Variants", "[policy]")
{

  SECTION("1. Unique Storage Policy (std::unique_ptr)")
  {
    frozen_vector_builder<int, unique_storage_policy<int>> builder;
    builder.push_back(100);
    builder.push_back(200);

    // Test Deep Copy (unique_storage_policy supports copy())
    auto builder_copy = builder;
    REQUIRE(builder_copy[0] == 100);
    builder_copy[0] = 999;
    REQUIRE(builder[0] == 100); // Verify distinct memory

    // Test Freeze (Transition unique -> shared)
    auto frozen = std::move(builder).freeze();
    REQUIRE(frozen.size() == 2);
    REQUIRE(frozen[0] == 100);
  }

  SECTION("2. Local Shared Storage Policy (local_shared_ptr)")
  {
    using LocalBuilder =
        frozen_vector_builder<int, local_shared_storage_policy<int>>;
    LocalBuilder builder;

    builder.push_back(10);
    builder.push_back(20);

    // Test Freeze (Transition local -> local const)
    // Note: The freeze() call returns a frozen_vector<T, local_shared_ptr<const
    // T[]>>
    auto frozen = std::move(builder).freeze<local_shared_ptr<const int[]>>();

    REQUIRE(frozen.size() == 2);
    REQUIRE(frozen[0] == 10);

    // Test Shallow Copy (O(1) non-atomic)
    auto frozen_copy = frozen;
    REQUIRE(frozen_copy.data() == frozen.data());
  }
}
