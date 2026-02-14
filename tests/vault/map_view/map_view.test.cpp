#include "vault/map_view/map_view.hpp"
#include <boost/container/flat_map.hpp>
#include <catch2/catch_all.hpp>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

using namespace lib;

// =============================================================================
// Helpers for Heterogeneous Lookup
// =============================================================================

/**
 * @brief Custom transparent hasher for std::string_view lookup in
 * unordered_map.
 * * Required because std::hash<std::string> is NOT transparent by default.
 */
struct StringViewHash {
  using is_transparent = void; // Enable heterogeneous lookup

  std::size_t operator()(std::string_view sv) const noexcept
  {
    return std::hash<std::string_view>{}(sv);
  }

  std::size_t operator()(const std::string& s) const noexcept
  {
    return std::hash<std::string>{}(s);
  }

  std::size_t operator()(const char* s) const noexcept
  {
    return std::hash<std::string_view>{}(std::string_view(s));
  }
};

// Define fully compatible transparent container types
using TransparentMap = std::map<std::string, int, std::less<>>;
using TransparentFlatMap =
  boost::container::flat_map<std::string, int, std::less<>>;
using TransparentUnorderedMap =
  std::unordered_map<std::string, int, StringViewHash, std::equal_to<>>;

// =============================================================================
// Test Cases
// =============================================================================

template <typename T> void verify_value(T* ptr, const T& expected)
{
  REQUIRE(ptr != nullptr);
  CHECK(*ptr == expected);
}

// -----------------------------------------------------------------------------
// Test 1: Homogeneous Lookup (map_view<string, int>)
// * Works on ALL standard containers out of the box.
// -----------------------------------------------------------------------------
TEMPLATE_TEST_CASE("map_view: Homogeneous Lookup",
  "[map_view][homogeneous]",
  (std::map<std::string, int>),
  (std::unordered_map<std::string, int>),
  (boost::container::flat_map<std::string, int>))
{
  TestType container;
  container["alpha"] = 10;
  container["beta"]  = 20;

  // View uses std::string. Always valid.
  map_view<std::string, int> view{container};

  CHECK(view.size() == 2);
  verify_value(view.find("alpha"), 10);

  // Polyfill check: .at() should work even if container doesn't provide it via
  // vtable
  CHECK(view.at("beta") == 20);
  CHECK(view.contains("alpha"));
}

// -----------------------------------------------------------------------------
// Test 2: Heterogeneous Lookup (map_view<string_view, int>)
// * Only works on containers configured with transparent comparators/hashers.
// -----------------------------------------------------------------------------
TEMPLATE_TEST_CASE("map_view: Heterogeneous Lookup (Zero-Alloc)",
  "[map_view][heterogeneous]",
  TransparentMap,
  TransparentFlatMap,
  TransparentUnorderedMap) // Now includes unordered_map!
{
  TestType container;
  container.try_emplace("alpha", 10);
  container.try_emplace("beta", 20);

  // View uses string_view.
  // This Constructor verifies that c.find(string_view) is valid.
  map_view<std::string_view, int> view{container};

  SECTION("Find string_view literal")
  {
    verify_value(view.find("alpha"), 10);
    CHECK(view.contains("beta"));
    CHECK(view.find("delta") == nullptr);
  }

  SECTION("At() with string_view")
  {
    // Validates that map_view polyfills .at() using .find()
    // because std::map/unordered_map often lack .at(ViewKey).
    CHECK(view.at("alpha") == 10);
    CHECK_THROWS_AS(view.at("z"), std::out_of_range);
  }
}

// -----------------------------------------------------------------------------
// Test 3: Mutable View (mutable_map_view<string, int>)
// -----------------------------------------------------------------------------
TEMPLATE_TEST_CASE("mutable_map_view: Modification",
  "[mutable_map_view]",
  (std::map<std::string, int>),
  (std::unordered_map<std::string, int>))
{
  TestType                           container;
  mutable_map_view<std::string, int> view{container};

  SECTION("Insert or Assign")
  {
    auto [ptr, inserted] = view.insert_or_assign("key1", 100);
    CHECK(inserted);
    CHECK(*ptr == 100);
    CHECK(container.at("key1") == 100);

    auto [ptr2, inserted2] = view.insert_or_assign("key1", 200);
    CHECK_FALSE(inserted2);
    CHECK(*ptr2 == 200);
  }

  SECTION("Erasure")
  {
    view.insert_or_assign("A", 1);
    CHECK(view.size() == 1);

    view.erase("A");
    CHECK(view.empty());
  }
}
