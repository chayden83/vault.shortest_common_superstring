# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.25)

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

add_subdirectory(src/vault/algorithm)

if(VAULT_SHORTEST_COMMON_SUPERSTRING_BUILD_TESTS)
  add_subdirectory(tests/vault/algorithm)
endif()

if(VAULT_SHORTEST_COMMON_SUPERSTRING_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(VAULT_SHORTEST_COMMON_SUPERSTRING_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(VAULT_SHORTEST_COMMON_SUPERSTRING_CONFIG_FILE_PACKAGE)
  include(cmake/vault-config-file-package.cmake)
endif()
