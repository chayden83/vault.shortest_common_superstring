# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(ALL_EXAMPLES)

message(STATUS "${PROJECT_NAME}: Examples to be built: ${ALL_EXAMPLES}")

foreach(example ${ALL_EXAMPLES})
  add_executable(vault.shortest_common_superstring.examples.${example})
  
  target_sources(vault.shortest_common_superstring.examples.${example} PRIVATE
    ${example}.cpp
  )
  
  target_link_libraries(vault.shortest_common_superstring.examples.${example} PRIVATE
    vault::shortest_common_superstring
  )
endforeach()
