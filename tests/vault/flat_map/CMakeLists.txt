FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.flat_map.tests)

target_sources(vault.flat_map.tests PRIVATE
  flat_map.test.cpp
)

target_link_libraries(vault.flat_map.tests PRIVATE
  Catch2::Catch2WithMain
  vault::flat_map
)

add_test(vault.flat_map.tests vault.flat_map.tests)
