#include <boost/container/flat_map.hpp>
#include <catch2/catch_all.hpp>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include <vault/map_view/map_view.hpp>

using namespace lib;

/**
 * @brief Helper to verify if a pointer is valid and points to the expected
 * value.
 */
template <typename T> void verify_value(T* ptr, const T& expected)
{
  REQUIRE(ptr != nullptr);
  CHECK(*ptr == expected);
}

TEMPLATE_TEST_CASE("map_view: Read-only lookup and capacity",
  "[map_view][lookup]",
  (std::map<std::string, int, std::less<>>),
  (std::unordered_map<std::string, int>),
  (boost::container::flat_map<std::string, int>))
{
  TestType container;
  container.insert_or_assign("alpha", 10);
  container.insert_or_assign("beta", 20);
  container.insert_or_assign("gamma", 30);

  // We use string_view for the view's key type to exercise heterogeneous lookup
  map_view<std::string_view, int> view{container};

  SECTION("Capacity queries")
  {
    CHECK_FALSE(view.empty());
    CHECK(view.size() == 3);
  }

  SECTION("Successful lookup via find")
  {
    verify_value(view.find("alpha"), 10);
    verify_value(view.find("beta"), 20);
    verify_value(view.find("gamma"), 30);
  }

  SECTION("Failed lookup via find")
  {
    CHECK(view.find("delta") == nullptr);
    CHECK(view.find("") == nullptr);
  }

  SECTION("Lookup via at")
  {
    CHECK(view.at("alpha") == 10);
    CHECK_THROWS_AS(view.at("delta"), std::out_of_range);
  }

  SECTION("Presence checks")
  {
    CHECK(view.contains("alpha"));
    CHECK_FALSE(view.contains("delta"));
    CHECK(view.count("alpha") == 1);
    CHECK(view.count("delta") == 0);
  }

  SECTION("Value modification through view")
  {
    // map_view allows modifying values if V is not const
    if (auto* ptr = view.find("alpha")) {
      *ptr = 99;
    }
    CHECK(container.at("alpha") == 99);
  }
}

TEMPLATE_TEST_CASE("mutable_map_view: Structural mutation",
  "[mutable_map_view][mutate]",
  (std::map<std::string, int>),
  (std::unordered_map<std::string, int>),
  (boost::container::flat_map<std::string, int>))
{
  TestType                           container;
  mutable_map_view<std::string, int> view{container};

  SECTION("Insertion and Assignment")
  {
    auto [ptr1, inserted1] = view.insert_or_assign("key1", 100);
    CHECK(inserted1);
    verify_value(ptr1, 100);

    auto [ptr2, inserted2] = view.insert_or_assign("key1", 200);
    CHECK_FALSE(inserted2);
    verify_value(ptr2, 200);
    CHECK(container.size() == 1);
  }

  SECTION("Try Emplace")
  {
    auto [ptr1, inserted1] = view.try_emplace("key1", 10);
    CHECK(inserted1);

    auto [ptr2, inserted2] = view.try_emplace("key1", 20);
    CHECK_FALSE(inserted2);
    CHECK(*ptr2 == 10); // Should not have been updated
  }

  SECTION("Erasure")
  {
    view.insert_or_assign("a", 1);
    view.insert_or_assign("b", 2);

    CHECK(view.erase("a") == 1);
    CHECK(view.size() == 1);
    CHECK_FALSE(view.contains("a"));
    CHECK(view.erase("non-existent") == 0);
  }

  SECTION("Clear")
  {
    view.insert_or_assign("a", 1);
    view.insert_or_assign("b", 2);
    view.clear();
    CHECK(view.empty());
    CHECK(container.empty());
  }
}

TEST_CASE("map_view: Heterogeneous lookup safety", "[map_view][heterogeneous]")
{
  // Testing specific transparent comparator requirement
  std::map<std::string, int, std::less<>> transparent_map;
  std::map<std::string, int>              standard_map;

  transparent_map["test"] = 1;
  standard_map["test"]    = 1;

  // This should compile because std::less<> provides find(string_view)
  map_view<std::string_view, int> v1{transparent_map};
  CHECK(v1.contains("test"));

  // The following would fail to compile due to concepts::map_compatible:
  // map_view<std::string_view, int> v2{standard_map};
}
