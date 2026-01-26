FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.frozen_vector.tests)

target_sources(vault.frozen_vector.tests PRIVATE
  frozen_vector.test.cpp
)

target_link_libraries(vault.frozen_vector.tests PRIVATE
  Catch2::Catch2WithMain
  vault::frozen_vector
)

add_test(vault.frozen_vector.tests vault.frozen_vector.tests)
