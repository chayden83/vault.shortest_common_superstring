#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <vault/frozen_vector/frozen_vector.hpp>
#include <vault/frozen_vector/frozen_vector_builder.hpp>
#include <vault/frozen_vector/local_shared_ptr.hpp>
#include <vault/frozen_vector/local_shared_storage_policy.hpp>
#include <vault/frozen_vector/shared_storage_policy.hpp>
#include <vault/frozen_vector/unique_storage_policy.hpp>

using namespace frozen;

// ============================================================================
// HELPER: Instance Counter
// ============================================================================

struct Tracker {
  static int count;
  int        id;

  Tracker()
      : id(++count)
  {}

  Tracker(const Tracker&)
      : id(++count)
  {}

  Tracker(Tracker&&) noexcept
      : id(++count)
  {}

  // FIX: Must provide move assignment for frozen_vector_builder constraints
  Tracker& operator=(const Tracker&) = default;
  Tracker& operator=(Tracker&&)      = default;

  ~Tracker() { count--; }

  std::string padding = "payload";
};

int Tracker::count = 0;

// ============================================================================
// PART 1: local_shared_ptr TESTS
// ============================================================================

TEST_CASE("local_shared_ptr: Basic Lifecycle", "[local_ptr]")
{
  Tracker::count = 0;

  SECTION("Default Construction")
  {
    local_shared_ptr<Tracker> ptr;
    REQUIRE_FALSE(ptr);
    REQUIRE(ptr.use_count() == 0);
    REQUIRE(ptr.get() == nullptr);
  }

  SECTION("Nullptr Construction & Assignment")
  {
    local_shared_ptr<Tracker> ptr(nullptr);
    REQUIRE_FALSE(ptr);
    local_shared_ptr<Tracker> ptr2;
    ptr2 = nullptr;
    REQUIRE_FALSE(ptr2);
  }

  SECTION("Construction from Raw Pointer")
  {
    {
      auto*                     raw = new Tracker();
      local_shared_ptr<Tracker> ptr(raw);
      REQUIRE(ptr.use_count() == 1);
      REQUIRE(Tracker::count == 1);
    }
    REQUIRE(Tracker::count == 0);
  }

  SECTION("Construction from unique_ptr")
  {
    {
      auto                      uptr = std::make_unique<Tracker>();
      local_shared_ptr<Tracker> ptr(std::move(uptr));
      REQUIRE(ptr.use_count() == 1);
      REQUIRE(Tracker::count == 1);
    }
    REQUIRE(Tracker::count == 0);
  }

  SECTION("Factory Construction")
  {
    {
      // Allocate array of 3 Trackers
      // The factory now performs default-initialization, which calls the
      // Tracker constructor 3 times.
      auto ptr = allocate_local_shared_for_overwrite<Tracker[]>(
          3, std::allocator<Tracker>{}
      );

      // REMOVED: for(int i=0; i<3; ++i) new (&ptr[i]) Tracker();
      // We no longer manually construct; they are already alive.

      REQUIRE(ptr.use_count() == 1);
      REQUIRE(Tracker::count == 3);
    }
    // ptr goes out of scope -> ref count 0 -> destructors called
    REQUIRE(Tracker::count == 0);
  }
}

TEST_CASE("local_shared_ptr: Copy and Move Semantics", "[local_ptr]")
{
  Tracker::count                = 0;
  auto*                     raw = new Tracker();
  local_shared_ptr<Tracker> ptr1(raw);

  SECTION("Copy Construction")
  {
    local_shared_ptr<Tracker> ptr2 = ptr1;
    REQUIRE(ptr1.use_count() == 2);
    REQUIRE(ptr2.use_count() == 2);
    REQUIRE(ptr1.get() == ptr2.get());
  }

  SECTION("Move Construction")
  {
    local_shared_ptr<Tracker> ptr2 = std::move(ptr1);
    REQUIRE(ptr2.use_count() == 1);
    REQUIRE(ptr1.get() == nullptr);
  }
}

TEST_CASE("local_shared_ptr: Aliasing", "[local_ptr]")
{
  struct Composite {
    int x;
    int y;
  };

  auto owner = std::make_unique<Composite>();
  owner->x   = 10;
  owner->y   = 20;

  local_shared_ptr<Composite> main_ptr(std::move(owner));

  SECTION("Aliasing Constructor")
  {
    // Points to y, shares ref count with main_ptr
    local_shared_ptr<int> alias_ptr(main_ptr, &main_ptr->y);

    REQUIRE(alias_ptr.use_count() == 2);
    REQUIRE(*alias_ptr == 20); // Requires operator*

    main_ptr.reset();
    REQUIRE(alias_ptr.use_count() == 1);
    REQUIRE(*alias_ptr == 20);
  }
}

TEST_CASE("local_shared_ptr: Type Conversion", "[local_ptr]")
{
  local_shared_ptr<int> mutable_ptr(new int(42));

  SECTION("Copy Converting Constructor")
  {
    local_shared_ptr<const int> const_ptr = mutable_ptr;
    REQUIRE(const_ptr.use_count() == 2);
    REQUIRE(*const_ptr == 42);
  }
}

// ============================================================================
// PART 2: frozen_vector TESTS
// ============================================================================

using LocalBuilder =
    frozen_vector_builder<int, local_shared_storage_policy<int>>;
using SharedBuilder = frozen_vector_builder<int, shared_storage_policy<int>>;
using UniqueBuilder = frozen_vector_builder<int, unique_storage_policy<int>>;

TEST_CASE("frozen_vector: Freeze Semantics", "[vector]")
{

  SECTION("1. Local Builder -> Local Frozen Vector")
  {
    LocalBuilder builder;
    builder.push_back(100);
    auto vec = std::move(builder).freeze();
    static_assert(std::is_same_v<
                  typename decltype(vec)::handle_type,
                  local_shared_ptr<const int[]>>);
    REQUIRE(vec[0] == 100);
  }

  SECTION("2. Shared Builder -> Shared Frozen Vector")
  {
    SharedBuilder builder;
    builder.push_back(200);
    auto vec = std::move(builder).freeze();
    static_assert(std::is_same_v<
                  typename decltype(vec)::handle_type,
                  std::shared_ptr<const int[]>>);
    REQUIRE(vec[0] == 200);
  }

  SECTION("3. Unique Builder -> Shared Frozen Vector (Default Upgrade)")
  {
    UniqueBuilder builder;
    builder.push_back(300);
    auto vec = std::move(builder).freeze();
    static_assert(std::is_same_v<
                  typename decltype(vec)::handle_type,
                  std::shared_ptr<const int[]>>);
    REQUIRE(vec[0] == 300);
  }
}

TEST_CASE("frozen_vector: Freeze Overrides", "[vector]")
{

  SECTION("Override: Unique Builder -> Local Frozen Vector")
  {
    // This validates the requirement "convert from std::unique_ptr to
    // local_shared_ptr"
    UniqueBuilder builder;
    builder.push_back(500);
    builder.push_back(600);

    // Explicitly requesting local_shared_ptr<const int[]>
    // Freeze traits should:
    // 1. Release unique_ptr (getting raw int*)
    // 2. Construct local_shared_ptr from raw int*
    auto vec = std::move(builder).freeze<local_shared_ptr<const int[]>>();

    static_assert(std::is_same_v<
                  typename decltype(vec)::handle_type,
                  local_shared_ptr<const int[]>>);
    REQUIRE(vec.size() == 2);
    REQUIRE(vec[0] == 500);
    REQUIRE(vec[1] == 600);

    // Check ref counting works on the result
    auto copy = vec;
    // local_shared_ptr use_count check requires accessing the handle, which
    // isn't exposed publicly but we verify data integrity
    REQUIRE(copy[0] == 500);
  }
}

TEST_CASE("Integration: Deep Copy", "[integration]")
{
  SECTION("Builder Deep Copy")
  {
    SharedBuilder b1; // Now works because shared_storage_policy has copy()
    b1.push_back(1);
    auto b2 = b1;

    b2[0] = 2;
    REQUIRE(b1[0] == 1);
    REQUIRE(b2[0] == 2);
  }
}

TEST_CASE("Memory Leak Check", "[memory]")
{
  Tracker::count = 0;
  {
    using TrackerBuilder =
        frozen_vector_builder<Tracker, local_shared_storage_policy<Tracker>>;
    TrackerBuilder builder;
    builder.push_back(Tracker());
    {
      auto vec = std::move(builder).freeze();
      REQUIRE(Tracker::count == 1);
    }
    REQUIRE(Tracker::count == 0);
  }
  REQUIRE(Tracker::count == 0);
}
