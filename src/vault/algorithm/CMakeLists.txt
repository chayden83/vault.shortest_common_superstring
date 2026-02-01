# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#######################
### IMPORT PACKAGES ###
#######################

find_package(Boost COMPONENTS headers REQUIRED)

##############################
### FETCH EXTERNAL CONTENT ###
##############################

FetchContent_Declare(range-v3
  GIT_REPOSITORY https://github.com/ericniebler/range-v3.git
  GIT_TAG 0.12.0
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(range-v3)

FetchContent_Declare(fsst
  GIT_REPOSITORY https://github.com/cwida/fsst.git
  GIT_TAG HEAD
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(fsst)

########################
### DEFINE LIBRARIES ###
########################

vault_add_header_only_library(vault.shortest_common_superstring)

target_sources(
  vault.shortest_common_superstring
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${PROJECT_SOURCE_DIR}/include
      FILES
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/knuth_morris_pratt_overlap.hpp
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/knuth_morris_pratt_searcher.hpp
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/knuth_morris_pratt_failure_function.hpp
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/shortest_common_superstring.hpp
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/amac.hpp
        ${PROJECT_SOURCE_DIR}/include/vault/algorithm/fsst_dictionary.hpp
)

target_link_libraries(
  vault.shortest_common_superstring
    INTERFACE
      Boost::headers
      range-v3::range-v3
)
  
vault_add_library(vault.shortest_common_superstring.internal)

target_sources(
  vault.shortest_common_superstring.internal
  PRIVATE
    internal.cpp
    fsst_dictionary.cpp
)

target_sources(
  vault.shortest_common_superstring.internal
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${PROJECT_SOURCE_DIR}/src
      FILES
        ${PROJECT_SOURCE_DIR}/src/vault/algorithm/internal.hpp
)

target_link_libraries(
  vault.shortest_common_superstring.internal
  PRIVATE
    fsst
    vault.shortest_common_superstring
)
  
target_compile_definitions(
  vault.shortest_common_superstring.internal
  PRIVATE
    PROJECT_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)  

target_include_directories(
  vault.shortest_common_superstring.internal
  PRIVATE
    ${fsst_SOURCE_DIR}
)

vault_install_targets(
  TARGETS vault.shortest_common_superstring
)

vault_install_export()
