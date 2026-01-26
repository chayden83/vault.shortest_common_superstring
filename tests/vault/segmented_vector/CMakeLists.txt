FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.segmented_vector.tests)

target_sources(vault.segmented_vector.tests PRIVATE
  segmented_vector.test.cpp
)

target_link_libraries(vault.segmented_vector.tests PRIVATE
  Catch2::Catch2WithMain
  vault::segmented_vector
)

add_test(vault.segmented_vector.tests vault.segmented_vector.tests)
