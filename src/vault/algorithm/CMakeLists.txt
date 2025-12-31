# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(Boost COMPONENTS headers REQUIRED)

vault_add_header_only_library(vault.shortest_common_superstring)

target_sources(
  vault.shortest_common_superstring
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${PROJECT_SOURCE_DIR}/include
      FILES
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/knuth_morris_pratt_searcher.hpp
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/knuth_morris_pratt_failure_function.hpp
)

target_link_libraries(
  vault.shortest_common_superstring
  INTERFACE
    Boost::headers
)
  
vault_add_library(vault.shortest_common_superstring.internal)

target_sources(
  vault.shortest_common_superstring.internal
  PRIVATE
    internal.cpp
)

target_sources(
  vault.shortest_common_superstring.internal
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${PROJECT_SOURCE_DIR}/src
      FILES
        ${PROJECT_SOURCE_DIR}/src/vault/algorithm/internal.hpp
)

target_compile_definitions(
  vault.shortest_common_superstring.internal
  PRIVATE
    PROJECT_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)  
  
vault_install_targets(
  TARGETS vault.shortest_common_superstring
)

vault_install_export()
