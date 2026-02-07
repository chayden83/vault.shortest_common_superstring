vault_add_header_only_library(vault.static_index)

target_sources(vault.static_index PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/static_index/static_index.hpp
)

target_link_libraries(vault.static_index INTERFACE
  PTHASH
)

# vault_install_targets(
#   TARGETS vault.static_index
# )

# vault_install_export()
