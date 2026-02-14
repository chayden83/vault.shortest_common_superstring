FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)

FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG        v24.3.25
)

FetchContent_MakeAvailable(Catch2)
FetchContent_MakeAvailable(flatbuffers)

include("${flatbuffers_SOURCE_DIR}/CMake/BuildFlatBuffers.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/LazyFlatBuffers.cmake")

add_lazy_flatbuffers_library(
    TARGET          vault.flatbuffers.tests.schema
    SCHEMAS         "${CMAKE_CURRENT_SOURCE_DIR}/monster.fbs"
                    "${CMAKE_CURRENT_SOURCE_DIR}/world.fbs"
    INCLUDE         "${CMAKE_CURRENT_SOURCE_DIR}"
    SCRIPT_PATH     "${CMAKE_SOURCE_DIR}/scripts/generate_lazy_traits.py"
)

add_executable(vault.flatbuffers.tests)

target_sources(vault.flatbuffers.tests PRIVATE
  flatbuffers.test.cpp
)

target_include_directories(vault.flatbuffers.tests PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(vault.flatbuffers.tests PRIVATE
  Catch2::Catch2WithMain
  FlatBuffers::FlatBuffers
  vault::flatbuffers
)

add_dependencies(vault.flatbuffers.tests
  vault.flatbuffers.tests.schema
)

add_test(vault.flatbuffers.tests vault.flatbuffers.tests)
