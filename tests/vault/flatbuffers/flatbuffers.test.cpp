/**
 * @file table_all.test.cpp
 * @brief Comprehensive tests for table functionality, policies, and
 * transitive resolution.
 */
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <flatbuffers/flatbuffers.h>

#include <vault/flatbuffers/flatbuffers.hpp>

#include "monster_generated.h"
#include "monster_traits.hpp"
#include "world_generated.h"
#include "world_traits.hpp"

namespace {

  /**
   * @brief Helper to create a complex buffer: Zone -> Monster -> Equipment
   */
  auto create_test_buffer() -> std::vector<uint8_t> {
    auto fbb = flatbuffers::FlatBufferBuilder{};

    // 1. Create Equipment (Nested Level 2)
    auto eq_name = fbb.CreateString("Excalibur");
    auto eq      = Game::CreateEquipment(fbb, eq_name, 99);
    fbb.Finish(eq);
    auto eq_bytes = std::vector<uint8_t>(fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());

    // 2. Create Monster (Nested Level 1)
    fbb.Clear();
    auto m_name = fbb.CreateString("Dragon");
    auto m_gear = fbb.CreateVector(eq_bytes);
    auto m      = Game::CreateMonster(fbb, m_name, 500, m_gear);
    fbb.Finish(m);
    auto m_bytes = std::vector<uint8_t>(fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());

    // 3. Create Zone (Root)
    fbb.Clear();
    auto z_name = fbb.CreateString("Forbidden Forest");
    auto z_boss = fbb.CreateVector(m_bytes);
    auto z      = Game::World::CreateZone(fbb, z_name, z_boss);
    fbb.Finish(z);

    return {fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};
  }

} // namespace

// -----------------------------------------------------------------------------
// Core Functionality & NTTP Optimization
// -----------------------------------------------------------------------------

TEST_CASE("table core features", "[table]") {
  const auto buffer = create_test_buffer();

  SECTION("initialization and root access") {
    auto zone = vault::fb::table<Game::World::Zone>::create(buffer.data(), buffer.size());

    REQUIRE(zone.has_value());
    // Standard FlatBuffer accessor via operator->
    REQUIRE(zone->get()->name()->str() == "Forbidden Forest");
  }

  SECTION("memoization and pointer stability") {
    auto zone = vault::fb::table<Game::World::Zone>::create(buffer.data(), buffer.size());

    // Use auto-generated traits for single-argument call
    auto monster1 = zone->get_nested<&Game::World::Zone::boss>();
    auto monster2 = zone->get_nested<&Game::World::Zone::boss>();

    REQUIRE(monster1.has_value());
    REQUIRE(monster2.has_value());
    // Verify memoization: internal pointer is identical
    REQUIRE(monster1->get() == monster2->get());
  }
}

// -----------------------------------------------------------------------------
// Policy Tests
// -----------------------------------------------------------------------------

TEST_CASE("table policy variations", "[table][policy]") {
  const auto buffer = create_test_buffer();

  SECTION("single-threaded policy (no mutex overhead)") {
    using fast_zone = vault::fb::table<Game::World::Zone, vault::fb::unsynchronized_t>;

    auto zone = fast_zone::create(buffer.data(), buffer.size());
    REQUIRE(zone.has_value());

    auto monster = zone->get_nested<&Game::World::Zone::boss>();
    REQUIRE(monster.has_value());
    REQUIRE(monster->get()->name()->str() == "Dragon");
  }
}

// -----------------------------------------------------------------------------
// Transitive Dependency Resolution
// -----------------------------------------------------------------------------

TEST_CASE("transitive dependency resolution", "[table][transitive]") {
  const auto buffer = create_test_buffer();

  auto  zone_opt = vault::fb::table<Game::World::Zone>::create(buffer.data(), buffer.size());
  auto& zone     = *zone_opt;

  // Level 1: Resolve Monster from Zone (Cross-namespace/file)
  auto monster = zone.get_nested<&Game::World::Zone::boss>();
  REQUIRE(monster.has_value());
  REQUIRE(monster->get()->name()->str() == "Dragon");

  // Level 2: Resolve Equipment from Monster
  // This uses the trait defined in monster_traits.hpp
  auto gear = monster->get_nested<&Game::Monster::equipped_gear>();
  REQUIRE(gear.has_value());
  REQUIRE(gear->get()->damage() == 99);
  REQUIRE(gear->get()->name()->str() == "Excalibur");
}

// -----------------------------------------------------------------------------
// Type Override (Bypassing Traits)
// -----------------------------------------------------------------------------

TEST_CASE("explicit type override", "[table][traits]") {
  const auto buffer = create_test_buffer();
  auto       zone   = vault::fb::table<Game::World::Zone>::create(buffer.data(), buffer.size());

  // Explicitly providing the type bypasses trait lookup
  auto monster = zone->get_nested<&Game::World::Zone::boss, Game::Monster>();

  REQUIRE(monster.has_value());
  REQUIRE(monster->get()->name()->str() == "Dragon");
}
