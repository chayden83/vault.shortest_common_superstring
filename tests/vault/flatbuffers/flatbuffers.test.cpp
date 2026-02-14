/**
 * @file flatbuffers_test.cpp
 * @brief Unit tests for the lazy_wrapper implementation using NTTP accessors.
 */
#include <catch2/catch_test_macros.hpp>
#include <flatbuffers/flatbuffers.h>
#include <vector>

// Include the schema-generated code
#include "monster_generated.h"

// Include the library under test
#include <vault/flatbuffers/flatbuffers.hpp>

namespace {

  /**
   * @brief Helper to create a nested flatbuffer payload for testing.
   * Creates a 'Monster' table containing a nested 'Equipment' buffer.
   */
  auto create_monster_buffer() -> std::vector<uint8_t>
  {
    auto fbb = flatbuffers::FlatBufferBuilder{};

    // 1. Create the Nested Table (Equipment)
    // We create the inner buffer first so we can flatten it into bytes
    auto eq_name = fbb.CreateString("Sword of Laziness");

    auto eq_builder = Game::EquipmentBuilder{fbb};
    eq_builder.add_name(eq_name);
    eq_builder.add_damage(50);
    auto equipment = eq_builder.Finish();

    fbb.Finish(equipment);

    // Copy the nested buffer bytes to a separate vector
    // This simulates the "blob" nature of nested flatbuffers
    auto nested_bytes = std::vector<uint8_t>{
      fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};

    // 2. Create the Outer Table (Monster)
    // We must clear the builder to start a new buffer
    fbb.Clear();

    auto m_name = fbb.CreateString("Goblin");
    auto m_gear = fbb.CreateVector(nested_bytes);

    auto m_builder = Game::MonsterBuilder{fbb};
    m_builder.add_name(m_name);
    m_builder.add_hp(100);
    m_builder.add_equipped_gear(m_gear);

    auto monster = m_builder.Finish();
    fbb.Finish(monster);

    return {fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};
  }

} // namespace

TEST_CASE("lazy_wrapper basic functionality", "[lazy_wrapper]")
{
  const auto buffer = create_monster_buffer();

  SECTION("greedy envelope verification succeeds")
  {
    // Should succeed because the outer monster is valid
    // The nested buffer is NOT verified at this stage
    auto monster =
      lazyfb::lazy_wrapper<Game::Monster>::create(buffer.data(), buffer.size());

    REQUIRE(monster.has_value());

    // Access standard field via operator-> or get()
    REQUIRE(monster->get()->hp() == 100);
    REQUIRE(monster->get()->name()->str() == "Goblin");
  }

  SECTION("lazy nested access verifies on demand")
  {
    auto monster =
      lazyfb::lazy_wrapper<Game::Monster>::create(buffer.data(), buffer.size());

    REQUIRE(monster.has_value());

    // First access: Triggers verification
    // NOTE: Accessor is now passed as a Non-Type Template Parameter
    auto gear =
      monster->get_nested<&Game::Monster::equipped_gear, Game::Equipment>();

    REQUIRE(gear.has_value());
    REQUIRE(gear->get()->damage() == 50);
    REQUIRE(gear->get()->name()->str() == "Sword of Laziness");
  }

  SECTION("memoization returns same context")
  {
    auto monster =
      lazyfb::lazy_wrapper<Game::Monster>::create(buffer.data(), buffer.size());

    REQUIRE(monster.has_value());

    // Both calls use the same static ID generation and context
    auto gear_1 =
      monster->get_nested<&Game::Monster::equipped_gear, Game::Equipment>();
    auto gear_2 =
      monster->get_nested<&Game::Monster::equipped_gear, Game::Equipment>();

    REQUIRE(gear_1.has_value());
    REQUIRE(gear_2.has_value());

    // Ensure both wrappers point to the same underlying FlatBuffer memory
    REQUIRE(gear_1->get() == gear_2->get());
  }
}

TEST_CASE("lazy_wrapper single threaded policy", "[lazy_wrapper]")
{
  const auto buffer = create_monster_buffer();

  // Use the single_threaded policy for performance when thread safety is not
  // required
  using fast_wrapper =
    lazyfb::lazy_wrapper<Game::Monster, lazyfb::policies::single_threaded>;

  auto monster = fast_wrapper::create(buffer.data(), buffer.size());

  REQUIRE(monster.has_value());

  auto gear =
    monster->get_nested<&Game::Monster::equipped_gear, Game::Equipment>();

  REQUIRE(gear.has_value());
  REQUIRE(gear->get()->damage() == 50);
}
