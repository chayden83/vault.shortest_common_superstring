/**
 * @file flatbuffers_test.cpp
 * @brief Unit tests for the lazy_wrapper implementation using Traits.
 */
#include <catch2/catch_test_macros.hpp>
#include <flatbuffers/flatbuffers.h>
#include <vector>

// Include the schema-generated code
#include "monster_generated.h"

// Include the library under test
#include <vault/flatbuffers/flatbuffers.hpp>

// -----------------------------------------------------------------------------
// Traits Definition
// -----------------------------------------------------------------------------
// We must specialize the trait in the lazyfb::traits namespace
// to map the accessor to the concrete type.
namespace lazyfb::traits {

  template <> struct nested_type<&Game::Monster::equipped_gear> {
    using type = Game::Equipment;
  };

} // namespace lazyfb::traits

namespace {

  /**
   * @brief Helper to create a nested flatbuffer payload for testing.
   */
  auto create_monster_buffer() -> std::vector<uint8_t>
  {
    auto fbb = flatbuffers::FlatBufferBuilder{};

    auto eq_name    = fbb.CreateString("Sword of Laziness");
    auto eq_builder = Game::EquipmentBuilder{fbb};
    eq_builder.add_name(eq_name);
    eq_builder.add_damage(50);
    auto equipment = eq_builder.Finish();
    fbb.Finish(equipment);

    auto nested_bytes = std::vector<uint8_t>{
      fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};

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

TEST_CASE("lazy_wrapper traits functionality", "[lazy_wrapper]")
{
  const auto buffer = create_monster_buffer();

  SECTION("lazy nested access via traits")
  {
    auto monster =
      lazyfb::lazy_wrapper<Game::Monster>::create(buffer.data(), buffer.size());
    REQUIRE(monster.has_value());

    // TRAITS USAGE:
    // Because we defined the trait specialization above,
    // we can omit the explicit <Game::Equipment> type here.
    auto gear = monster->get_nested<&Game::Monster::equipped_gear>();

    REQUIRE(gear.has_value());
    REQUIRE(gear->get()->damage() == 50);
    REQUIRE(gear->get()->name()->str() == "Sword of Laziness");
  }

  SECTION("lazy nested access via explicit type override")
  {
    auto monster =
      lazyfb::lazy_wrapper<Game::Monster>::create(buffer.data(), buffer.size());
    REQUIRE(monster.has_value());

    // EXPLICIT USAGE:
    // We can still explicitly provide the type if we want to bypass traits
    // or if a trait hasn't been defined for a specific field.
    auto gear =
      monster->get_nested<&Game::Monster::equipped_gear, Game::Equipment>();

    REQUIRE(gear.has_value());
    REQUIRE(gear->get()->damage() == 50);
  }
}
