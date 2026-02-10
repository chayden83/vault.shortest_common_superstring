FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.static_index.tests)

target_sources(vault.static_index.tests PRIVATE
  static_index.test.cpp
  static_index.test.laptop.cpp
  static_index.test.extensive.cpp
)

target_link_libraries(vault.static_index.tests PRIVATE
  Catch2::Catch2WithMain
  vault::static_index
)

add_test(vault.static_index.tests vault.static_index.tests)
