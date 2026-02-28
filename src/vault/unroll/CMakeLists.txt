vault_add_header_only_library(vault.unroll)

target_sources(vault.flatbuffers PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/unroll/unroll.hpp
)

vault_install_targets(
  TARGET vault.unroll
)

vault_install_export()
