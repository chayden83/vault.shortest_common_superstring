FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.allocators.tests)

target_sources(vault.allocators.tests PRIVATE
  allocators.test.cpp
)

target_link_libraries(vault.allocators.tests PRIVATE
  Catch2::Catch2WithMain
  vault::allocators
)

add_test(vault.allocators.tests vault.allocators.tests)
