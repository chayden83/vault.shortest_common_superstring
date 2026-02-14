FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.map_view.tests)

target_sources(vault.map_view.tests PRIVATE
  map_view.test.cpp
)

target_link_libraries(vault.map_view.tests PRIVATE
  Catch2::Catch2WithMain
  vault::map_view
)

add_test(vault.map_view.tests vault.map_view.tests)
