vault_add_library(vault.flat_map)

target_sources(vault.flat_map PRIVATE
  implicit_btree_layout_policy.cpp
)

target_sources(vault.flat_map PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/concepts.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/aliases.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/eytzinger_layout_policy.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/implicit_btree_layout_policy.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/layout_iterator.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/layout_map.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/sorted_layout_policy.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/flat_map/utilities.hpp
)

target_compile_definitions(vault.flat_map PUBLIC
  LAYOUT_USE_AVX2=1
)

vault_install_targets(
  TARGETS vault.flat_map
)

vault_install_export()
