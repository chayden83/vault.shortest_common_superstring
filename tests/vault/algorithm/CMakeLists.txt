# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(INSTALL_GTEST OFF) # Disable GoogleTest installation

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # release-1.14.0
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(Catch2)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(vault.shortest_common_superstring.tests)

target_sources(vault.shortest_common_superstring.tests PRIVATE
  shortest_common_superstring.test.cpp
)

target_link_libraries(vault.shortest_common_superstring.tests PRIVATE
  GTest::gtest
  GTest::gmock
  GTest::gtest_main
  vault::shortest_common_superstring
  vault::shortest_common_superstring.internal
)

gtest_add_tests(vault.shortest_common_superstring.tests "" AUTO)

add_executable(vault.amac.tests)

target_sources(vault.amac.tests PRIVATE
  amac.test.cpp
)

target_link_libraries(vault.amac.tests PRIVATE
  Catch2::Catch2WithMain
  vault::shortest_common_superstring
  vault::shortest_common_superstring.internal
)

add_test(vault.amac.tests vault.amac.tests)
