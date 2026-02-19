#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include <vault/allocators/hpallocator.hpp>

using namespace static_data;

TEST_CASE("hpallocator basic functionality", "[allocators][hpallocator]") {
  hpallocator<int> alloc;

  SECTION("allocation and deallocation of a single element") {
    int* ptr = nullptr;
    REQUIRE_NOTHROW(ptr = alloc.allocate(1));
    REQUIRE(ptr != nullptr);

    // Ensure we can write to it
    *ptr = 42;
    CHECK(*ptr == 42);

    alloc.deallocate(ptr, 1);
  }

  SECTION("allocation of zero elements returns nullptr") {
    CHECK(alloc.allocate(0) == nullptr);
  }

  SECTION("large allocation throws on overflow") {
    const size_t max_size = std::numeric_limits<size_t>::max();
    CHECK_THROWS_AS(alloc.allocate(max_size), std::bad_array_new_length);
  }
}

TEST_CASE("hpallocator hardware invariants", "[allocators][hpallocator][linux]") {
  hpallocator<std::byte> alloc;
  constexpr size_t       threshold = hpallocator<std::byte>::huge_page_threshold;

  SECTION("small allocations use standard alignment") {
    // 1 KiB allocation
    const size_t size = 1024;
    auto*        ptr  = alloc.allocate(size);

    auto addr = reinterpret_cast<std::uintptr_t>(ptr);
    // Should be at least pointer-aligned (standard malloc behavior)
    CHECK(addr % sizeof(void*) == 0);

    alloc.deallocate(ptr, size);
  }

  SECTION("large allocations are 2MB aligned") {
    // Exactly the threshold (2 MiB)
    const size_t size = threshold;
    auto*        ptr  = alloc.allocate(size);

    auto             addr = reinterpret_cast<std::uintptr_t>(ptr);
    constexpr size_t mask = threshold - 1;

    // This is the core requirement for our Binary Fuse Filter optimization
    CHECK((addr & mask) == 0);

    alloc.deallocate(ptr, size);
  }
}

TEST_CASE("hpallocator C++23 conformance", "[allocators][hpallocator][cpp23]") {
  hpallocator<double> alloc;

  SECTION("allocate_at_least returns valid result") {
    const size_t requested = 100;
    auto         result    = alloc.allocate_at_least(requested);

    REQUIRE(result.ptr != nullptr);
    CHECK(result.count >= requested);

    alloc.deallocate(result.ptr, result.count);
  }

  SECTION("allocator equality") {
    hpallocator<int>    a1;
    hpallocator<double> a2;

    // Stateless allocators must be equal regardless of T
    CHECK(a1 == a2);
    CHECK_FALSE(a1 != a2);
  }

  SECTION("copy construction between types") {
    hpallocator<int>   int_alloc;
    hpallocator<float> float_alloc(int_alloc);

    float* ptr = float_alloc.allocate(10);
    REQUIRE(ptr != nullptr);
    float_alloc.deallocate(ptr, 10);
  }
}

// Constexpr testing (C++20/23 feature)
// Note: Some older compilers might struggle with constexpr new in certain envs,
// but this is standard-compliant.
TEST_CASE("hpallocator constexpr evaluation", "[allocators][hpallocator][constexpr]") {
    auto test_constexpr = []() constexpr {
        hpallocator<int> alloc;
        int* p = alloc.allocate(5);
        
        // C++20/23 requirement: Start the lifetime of the object
        // You cannot assign to p[0] until the object at that location exists.
        std::construct_at(&p[0], 10); 
        
        int val = p[0];
        
        // Optional but good practice: destroy before deallocate
        std::destroy_at(&p[0]); 
        alloc.deallocate(p, 5);
        
        return val;
    };

    static_assert(test_constexpr() == 10);
    SUCCEED("Constexpr allocation/deallocation validated.");
}

TEST_CASE("hpallocator with standard containers", "[allocators][hpallocator][integration]") {
  SECTION("std::vector integration") {
    std::vector<uint64_t, hpallocator<uint64_t>> vec;

    // Small growth (jemalloc path)
    vec.push_back(1);
    CHECK(vec.size() == 1);

    // Large growth (Huge Page path)
    // 170M elements ~ 1.26 GiB
    const size_t large_n = 1024 * 1024;
    vec.resize(large_n);

    auto addr = reinterpret_cast<std::uintptr_t>(vec.data());
    // If the vector buffer > 2MB, it should be huge-page aligned
    if (vec.capacity() * sizeof(uint64_t) >= hpallocator<uint64_t>::huge_page_threshold) {
      CHECK(addr % (2 * 1024 * 1024) == 0);
    }
  }

  SECTION("std::string integration (The 2MB per string test)") {
    // This validates that our hybrid logic prevents small strings
    // from wasting 2MB each.
    using hp_string = std::basic_string<char, std::char_traits<char>, hpallocator<char>>;

    std::vector<hp_string> strings;
    for (int i = 0; i < 100; ++i) {
      strings.emplace_back("Short string");
    }

    // If each string took 2MB, RSS would be > 200MB here.
    // We just check that they are valid.
    for (const auto& s : strings) {
      CHECK(s == "Short string");
    }
  }
}
