find_package(zstd  REQUIRED)
find_package(Clang REQUIRED)

add_library(vault-tidy-plugin SHARED
  pointer-chasing-type-check.cpp
  vault-tidy-module.cpp
)

target_include_directories(vault-tidy-plugin PRIVATE
  ${CLANG_INCLUDE_DIRS}
)

target_link_libraries(vault-tidy-plugin PRIVATE
  clangTidy
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  LLVMSupport
)
