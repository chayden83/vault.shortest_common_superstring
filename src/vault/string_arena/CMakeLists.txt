vault_add_library(vault.string_arena)

target_sources(vault.string_arena PRIVATE
  ${PROJECT_SOURCE_DIR}/src/vault/string_arena/string_arena.cpp
)

target_sources(vault.flatbuffers PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/string_arena/string_arena.hpp
)

vault_install_targets(
  TARGET vault.string_arena
)

vault_install_export()
