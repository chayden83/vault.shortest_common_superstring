vault_add_header_only_library(vault.map_view)

target_sources(vault.map_view PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/map_view/map_view.hpp
)

vault_install_targets(
  TARGETS vault.map_view
)

vault_install_export()
