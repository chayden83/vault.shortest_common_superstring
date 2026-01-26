vault_add_header_only_library(vault.frozen_vector)

target_sources(vault.frozen_vector PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/concepts.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/freeze_traits.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/frozen_vector.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/local_shared_ptr.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/frozen_vector_builder.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/shared_storage_policy.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/unique_storage_policy.hpp
  ${PROJECT_SOURCE_DIR}/include/vault/frozen_vector/local_shared_storage_policy.hpp
)

vault_install_targets(
  TARGETS vault.frozen_vector
)

vault_install_export()
