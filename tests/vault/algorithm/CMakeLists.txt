# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(vault.shortest_common_superstring.tests)

target_sources(vault.shortest_common_superstring.tests PRIVATE
  shortest_common_superstring.test.cpp
)

target_link_libraries(vault.shortest_common_superstring.tests PRIVATE
  Catch2::Catch2WithMain
  vault::shortest_common_superstring
  vault::shortest_common_superstring.internal
)

add_test(vault.shortest_common_superstring.tests vault.shortest_common_superstring.tests)

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

add_executable(vault.fsst_dictionary.tests)

target_sources(vault.fsst_dictionary.tests PRIVATE
  fsst_dictionary.test.cpp
)

target_link_libraries(vault.fsst_dictionary.tests PRIVATE
  Catch2::Catch2WithMain
  vault::shortest_common_superstring
  vault::shortest_common_superstring.internal
)

add_test(vault.fsst_dictionary.tests vault.fsst_dictionary.tests)
