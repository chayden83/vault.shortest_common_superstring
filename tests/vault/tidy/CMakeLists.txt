set(pointer_chasing_test_cases
  "std-set"            "<set>"            "std::set<int>"
  "std-map"            "<map>"            "std::map<int, int>"
  "std-list"           "<list>"           "std::list<int>"
  "std-forward-list"   "<forward_list>"   "std::forward_list<int>"
  "std-unordered-map"  "<unordered_map>"  "std::unordered_map<int, int>"
  "std-unordered-set"  "<unordered_set>"  "std::unordered_set<int>"
)

set(pointer_chasing_test_template [=[
#include ${header}

int main() {
  ${type} { };
}
]=])  

while(pointer_chasing_test_cases)
  list(POP_FRONT pointer_chasing_test_cases case header type)

  string(CONFIGURE "${pointer_chasing_test_template}"
    pointer_chasing_test_content
  )
  
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${case}.cpp"
    "${pointer_chasing_test_content}"
  )

  add_test(NAME vault-tidy-${case} COMMAND
    clang-tidy-20
    -load=$<TARGET_FILE:vault-tidy-plugin>
    -checks=-*,vault-*
    -warnings-as-errors=*
    ${CMAKE_CURRENT_BINARY_DIR}/${case}.cpp
    --
    -std=c++23
  )

  set_tests_properties(vault-tidy-${case} PROPERTIES
    PASS_REGULAR_EXPRESSION ".*usage of pointer-chasing data structure detected.*"
  )
endwhile()
