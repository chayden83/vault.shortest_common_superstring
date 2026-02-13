FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG        v24.3.25
)

FetchContent_MakeAvailable(flatbuffers)

vault_add_header_only_library(vault.flatbuffers)

target_sources(vault.flatbuffers PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES
  ${PROJECT_SOURCE_DIR}/include/vault/flatbuffers/flatbuffers.hpp
)

target_link_libraries(vault.flatbuffers INTERFACE
  FlatBuffers::FlatBuffers
)

vault_install_targets(
  TARGET vault.flatbuffers
)

vault_install_export()
