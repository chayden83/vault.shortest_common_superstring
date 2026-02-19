vault_add_header_only_library(vault.allocators)

target_sources(vault.flatbuffers PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/allocators/hpallocator.hpp
)

vault_install_targets(
  TARGET vault.allocators
)

vault_install_export()
