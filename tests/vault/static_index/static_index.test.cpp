#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <catch2/generators/catch_generators.hpp>
#include <vault/static_index/static_index.hpp>

using namespace vault::containers;

namespace {
  std::vector<std::string> generate_items(size_t n) {
    std::vector<std::string> items;
    items.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      items.push_back("item_" + std::to_string(i));
    }
    return items;
  }
} // namespace

TEST_CASE("StaticIndex: Basic Functionality", "[static_index]") {
  static_index_builder builder;
  // Increase count to support pthash generation stability
  auto items = generate_items(300);

  builder.add_n(items);
  auto index = std::move(builder).build();

  REQUIRE_FALSE(index.empty());
  REQUIRE(index.memory_usage_bytes() > 0);

  for (const auto& item : items) {
    // Inlined 'result' which was only used once to check has_value()
    REQUIRE(index[item].has_value());
  }

  REQUIRE_FALSE(index["non_existent"].has_value());
}

TEST_CASE("StaticIndex: Permutation Verification", "[static_index][permutation]") {
  // Increase count to support pthash generation stability
  auto items = generate_items(300);

  SECTION("Build with Sink (Lambda)") {
    static_index_builder builder;
    builder.add_n(items);

    std::vector<size_t> permutation;
    permutation.reserve(items.size());

    // Capture the permutation via lambda sink
    // 'index' is used multiple times, 'sink' is unused and correctly 'ignored'
    auto [index, _] = std::move(builder).build([&](size_t slot) { permutation.push_back(slot); });

    REQUIRE(permutation.size() == items.size());

    // VERIFY: For each item 'i' added, its location in the index
    // must match permutation[i].
    for (size_t i = 0; i < items.size(); ++i) {
      // 'slot' is used twice (has_value and value comparison), so it remains a variable
      auto slot = index[items[i]];
      REQUIRE(slot.has_value());
      REQUIRE(*slot == permutation[i]);
    }
  }

  SECTION("Build with Output Iterator (Back Inserter)") {
    static_index_builder builder;
    builder.add_n(items);

    std::vector<size_t> permutation;

    // Capture via output iterator
    // 'index' is used multiple times, 'out_it' is unused and correctly 'ignored'
    auto [index, _] = std::move(builder).build(std::back_inserter(permutation));

    REQUIRE(permutation.size() == items.size());

    for (size_t i = 0; i < items.size(); ++i) {
      // 'slot' is used twice (has_value and value comparison), so it remains a variable
      auto slot = index[items[i]];
      REQUIRE(slot.has_value());
      REQUIRE(*slot == permutation[i]);
    }
  }

  SECTION("Data Reordering Use Case") {
    // This simulates the primary use case: reordering a parallel array
    // so it matches the index layout for cache locality.

    static_index_builder builder;
    builder.add_n(items);

    std::vector<std::string> reordered_items(items.size());
    size_t                   current_idx = 0;

    // 'index' is used multiple times, '_' is unused and correctly 'ignored'
    auto [index, _] = std::move(builder).build([&](size_t slot) {
      // Place the item at the slot dictated by the index
      reordered_items[slot] = items[current_idx];
      current_idx++;
    });

    // Verify that index lookup on the original item points to the
    // correct location in the reordered array.
    for (const auto& original_item : items) {
      // Inlined 'slot' as its value was used only once at the call site
      REQUIRE(reordered_items[*index[original_item]] == original_item);
    }
  }
}
