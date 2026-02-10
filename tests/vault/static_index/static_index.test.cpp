#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <iterator>
#include <string>
#include <vector>

#include <vault/static_index/static_index.hpp>

using namespace vault::containers;

TEST_CASE("StaticIndex: Basic Functionality", "[static_index]")
{
  static_index_builder     builder;
  std::vector<std::string> items = {"apple", "banana", "cherry", "date"};

  builder.add_n(items);
  auto index = std::move(builder).build();

  REQUIRE_FALSE(index.empty());
  REQUIRE(index.memory_usage_bytes() > 0);

  for (const auto& item : items) {
    auto result = index[item];
    REQUIRE(result.has_value());
  }

  REQUIRE_FALSE(index["elderberry"].has_value());
}

TEST_CASE(
  "StaticIndex: Permutation Verification", "[static_index][permutation]")
{
  std::vector<std::string> items = {
    "foo", "bar", "baz", "qux", "quux", "corge", "grault", "garply"};

  SECTION("Build with Sink (Lambda)")
  {
    static_index_builder builder;
    builder.add_n(items);

    std::vector<size_t> permutation;
    permutation.reserve(items.size());

    // Capture the permutation via lambda sink
    auto [index, sink] = std::move(builder).build(
      [&](size_t slot) { permutation.push_back(slot); });

    REQUIRE(permutation.size() == items.size());

    // VERIFY: For each item 'i' added, its location in the index
    // must match permutation[i].
    for (size_t i = 0; i < items.size(); ++i) {
      auto slot = index[items[i]];
      REQUIRE(slot.has_value());
      REQUIRE(*slot == permutation[i]);
    }
  }

  SECTION("Build with Output Iterator (Back Inserter)")
  {
    static_index_builder builder;
    builder.add_n(items);

    std::vector<size_t> permutation;

    // Capture via output iterator
    auto [index, out_it] =
      std::move(builder).build(std::back_inserter(permutation));

    REQUIRE(permutation.size() == items.size());

    for (size_t i = 0; i < items.size(); ++i) {
      auto slot = index[items[i]];
      REQUIRE(slot.has_value());
      REQUIRE(*slot == permutation[i]);
    }
  }

  SECTION("Data Reordering Use Case")
  {
    // This simulates the primary use case: reordering a parallel array
    // so it matches the index layout for cache locality.

    static_index_builder builder;
    builder.add_n(items);

    std::vector<std::string> reordered_items(items.size());
    size_t                   current_idx = 0;

    auto [index, _] = std::move(builder).build([&](size_t slot) {
      // Place the item at the slot dictated by the index
      reordered_items[slot] = items[current_idx];
      current_idx++;
    });

    // Verify that index lookup on the original item points to the
    // correct location in the reordered array.
    for (const auto& original_item : items) {
      size_t slot = *index[original_item];
      REQUIRE(reordered_items[slot] == original_item);
    }
  }
}

TEST_CASE("StaticIndex: Empty Index", "[static_index]")
{
  static_index_builder builder;

  SECTION("Standard Build")
  {
    auto index = std::move(builder).build();
    REQUIRE(index.empty());
    REQUIRE(index.memory_usage_bytes() == 0);
    REQUIRE_FALSE(index["anything"].has_value());
  }

  SECTION("Build with Sink")
  {
    bool sink_called = false;
    auto [index, _] =
      std::move(builder).build([&](size_t) { sink_called = true; });

    REQUIRE(index.empty());
    REQUIRE_FALSE(sink_called);
  }
}
