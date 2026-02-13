#include <catch2/catch_test_macros.hpp>
#include <flatbuffers/flatbuffers.h>

// Include the schema-generated code
#include "monster_generated.h"
#include <vault/flatbuffers/flatbuffers.hpp>

// Helper to create a nested flatbuffer payload
std::vector<uint8_t> create_monster_buffer()
{
  flatbuffers::FlatBufferBuilder fbb;

  // 1. Create the Nested Table (Equipment)
  auto                   eq_name = fbb.CreateString("Sword of Laziness");
  Game::EquipmentBuilder eq_builder(fbb);
  eq_builder.add_name(eq_name);
  eq_builder.add_damage(50);
  auto equipment = eq_builder.Finish();
  fbb.Finish(equipment);

  // Copy the nested buffer bytes
  std::vector<uint8_t> nested_bytes(
    fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());

  // 2. Create the Outer Table (Monster)
  fbb.Clear();
  auto m_name = fbb.CreateString("Goblin");
  auto m_gear = fbb.CreateVector(nested_bytes);

  Game::MonsterBuilder m_builder(fbb);
  m_builder.add_name(m_name);
  m_builder.add_hp(100);
  m_builder.add_equipped_gear(m_gear);
  auto monster = m_builder.Finish();
  fbb.Finish(monster);

  return {fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};
}

TEST_CASE("LazyWrapper Basic Functionality", "[lazy_wrapper]")
{
  auto buffer = create_monster_buffer();

  SECTION("Greedy Envelope Verification")
  {
    // Should succeed because the outer monster is valid
    auto monster =
      lazyfb::LazyWrapper<Game::Monster>::create(buffer.data(), buffer.size());
    REQUIRE(monster.has_value());

    // Access standard field
    REQUIRE((*monster)->hp() == 100);
    REQUIRE((*monster)->name()->str() == "Goblin");
  }

  SECTION("Lazy Nested Access")
  {
    auto monster =
      lazyfb::LazyWrapper<Game::Monster>::create(buffer.data(), buffer.size());

    // First access: Should trigger internal verification
    auto gear =
      monster->get_nested<Game::Equipment>(&Game::Monster::equipped_gear);

    REQUIRE(gear.has_value());
    REQUIRE((*gear)->damage() == 50);
    REQUIRE((*gear)->name()->str() == "Sword of Laziness");
  }

  SECTION("Memoization Check")
  {
    auto monster =
      lazyfb::LazyWrapper<Game::Monster>::create(buffer.data(), buffer.size());

    auto gear1 =
      monster->get_nested<Game::Equipment>(&Game::Monster::equipped_gear);
    auto gear2 =
      monster->get_nested<Game::Equipment>(&Game::Monster::equipped_gear);

    // Ensure both wrappers point to the same underlying FlatBuffer memory
    REQUIRE(gear1->get() == gear2->get());
  }
}

TEST_CASE("Single Threaded Policy", "[lazy_wrapper]")
{
  auto buffer = create_monster_buffer();

  using FastMonster =
    lazyfb::LazyWrapper<Game::Monster, lazyfb::policies::SingleThreaded>;
  auto monster = FastMonster::create(buffer.data(), buffer.size());

  REQUIRE(monster.has_value());
  auto gear =
    monster->get_nested<Game::Equipment>(&Game::Monster::equipped_gear);
  REQUIRE(gear.has_value());
  REQUIRE((*gear)->damage() == 50);
}
