FetchContent_Declare(vault.pthash
  GIT_REPOSITORY https://github.com/chayden83/jermp.pthash.git
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(vault.pthash)

vault_add_library(vault.static_index)

target_sources(vault.static_index PRIVATE
  static_index.cpp
)

target_sources(vault.static_index PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/static_index/static_index.hpp
)

target_link_libraries(vault.static_index PRIVATE
  vault::pthash
)

target_link_libraries(vault.static_index INTERFACE
  xxhash
)

vault_install_targets(
  TARGETS vault.static_index
)

vault_install_export()
