# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(BENCHMARK_ENABLE_TESTING OFF)
set(BENCHMARK_ENABLE_INSTALL OFF)

FetchContent_Declare(googlebench
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.4
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(googlebench)

set(ALL_BENCHMARKS
  #amac
  #flat_map
  frozen_vector
  #segmented_vector
  #shortest_common_superstring
)

message(STATUS "${PROJECT_NAME}: Benchmarks to be built: ${ALL_BENCHMARKS}")

add_custom_target(vault.shortest_common_superstring.benchmarks)

foreach(benchmark ${ALL_BENCHMARKS})
  add_executable(vault.shortest_common_superstring.benchmarks.${benchmark}.exe)
  
  target_sources(vault.shortest_common_superstring.benchmarks.${benchmark}.exe PRIVATE
    benchmarks.${benchmark}.cpp
  )

  target_link_libraries(vault.shortest_common_superstring.benchmarks.${benchmark}.exe PRIVATE
    range-v3::range-v3
    benchmark::benchmark
    benchmark::benchmark_main
    vault::flat_map
    vault::frozen_vector
    vault::segmented_vector
    vault::shortest_common_superstring
    vault::shortest_common_superstring.internal
  )

  add_custom_target(vault.shortest_common_superstring.benchmarks.${benchmark}
    COMMAND vault.shortest_common_superstring.benchmarks.${benchmark}.exe
  )

  add_dependencies(vault.shortest_common_superstring.benchmarks
    vault.shortest_common_superstring.benchmarks.${benchmark}
  )
endforeach()

if(PROJECT_IS_TOP_LEVEL)
  if(NOT TARGET vault.benchmarks)
    add_custom_target(vault.benchmarks)
  endif()

  if(NOT TARGET benchmarks)
    add_custom_target(benchmarks)
  endif()

  add_dependencies(vault.benchmarks vault.shortest_common_superstring.benchmarks)
  add_dependencies(      benchmarks                             vault.benchmarks)
endif()
