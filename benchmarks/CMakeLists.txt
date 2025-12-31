# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(BENCHMARK_ENABLE_TESTING OFF)
set(BENCHMARK_ENABLE_INSTALL OFF)

FetchContent_Declare(googlebench
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.4
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(tsl.robin-map
  GIT_REPOSITORY https://github.com/Tessil/robin-map.git
  GIT_TAG        v1.4.0 # Most recent release as of Sep. 2025
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(googlebench)
FetchContent_MakeAvailable(tsl.robin-map)

set(ALL_BENCHMARKS
  baseline
  std.unordered_map
  tsl.robin_map
)

message(STATUS "${PROJECT_NAME}: Benchmarks to be built: ${ALL_BENCHMARKS}")

add_custom_target(vault.histogram_algorithm.benchmarks)

foreach(benchmark ${ALL_BENCHMARKS})
  add_executable(vault.histogram_algorithm.benchmarks.${benchmark}.exe)
  
  target_sources(vault.histogram_algorithm.benchmarks.${benchmark}.exe PRIVATE
    benchmarks.${benchmark}.cpp
  )

  target_link_libraries(vault.histogram_algorithm.benchmarks.${benchmark}.exe PRIVATE
    tsl::robin_map
    benchmark::benchmark
    benchmark::benchmark_main
    vault::histogram_algorithm
    vault::histogram_algorithm.internal
  )

  add_custom_target(vault.histogram_algorithm.benchmarks.${benchmark}
    COMMAND vault.histogram_algorithm.benchmarks.${benchmark}.exe
  )

  add_dependencies(vault.histogram_algorithm.benchmarks
    vault.histogram_algorithm.benchmarks.${benchmark}
  )
endforeach()

if(PROJECT_IS_TOP_LEVEL)
  if(NOT TARGET vault.benchmarks)
    add_custom_target(vault.benchmarks)
  endif()

  if(NOT TARGET benchmarks)
    add_custom_target(benchmarks)
  endif()

  add_dependencies(vault.benchmarks vault.histogram_algorithm.benchmarks)
  add_dependencies(      benchmarks                     vault.benchmarks)
endif()
