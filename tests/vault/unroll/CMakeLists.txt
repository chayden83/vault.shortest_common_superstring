FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.unroll.tests)

target_sources(vault.unroll.tests PRIVATE
  unroll.test.cpp
)

target_link_libraries(vault.unroll.tests PRIVATE
  Catch2::Catch2WithMain
  vault::unroll
)

add_test(vault.unroll.tests vault.unroll.tests)
