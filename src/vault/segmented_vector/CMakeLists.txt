vault_add_header_only_library(vault.segmented_vector)

target_sources(vault.segmented_vector PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/segmented_vector/segmented_vector.hpp
)

vault_install_targets(
  TARGETS vault.segmented_vector
)

vault_install_export()
