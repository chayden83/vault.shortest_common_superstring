# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.30)

project(
  vault.shortest_common_superstring
  DESCRIPTION "A Vault library that implements set algorithms for using histograms."
  LANGUAGES C CXX
  VERSION 0.1.0
)

enable_testing()

include(FetchContent)
include(GNUInstallDirs)

include(cmake/vault-configure.cmake)

#add_subdirectory(src/vault/flat_map)
add_subdirectory(src/vault/algorithm)
add_subdirectory(src/vault/static_index)
add_subdirectory(src/vault/frozen_vector)
add_subdirectory(src/vault/segmented_vector)
add_subdirectory(src/vault/flatbuffers)
add_subdirectory(src/vault/map_view)
add_subdirectory(src/vault/allocators)

if(VAULT_SHORTEST_COMMON_SUPERSTRING_BUILD_TESTS)
#  add_subdirectory(tests/vault/flat_map)
  add_subdirectory(tests/vault/algorithm)
  add_subdirectory(tests/vault/static_index)
  add_subdirectory(tests/vault/frozen_vector)
  add_subdirectory(tests/vault/segmented_vector)
  add_subdirectory(tests/vault/flatbuffers)
  add_subdirectory(tests/vault/map_view)
  add_subdirectory(tests/vault/allocators)
endif()

if(VAULT_SHORTEST_COMMON_SUPERSTRING_BUILD_EXAMPLES)
#  add_subdirectory(examples)
endif()

if(VAULT_SHORTEST_COMMON_SUPERSTRING_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(VAULT_SHORTEST_COMMON_SUPERSTRING_CONFIG_FILE_PACKAGE)
  include(cmake/vault-config-file-package.cmake)
endif()
