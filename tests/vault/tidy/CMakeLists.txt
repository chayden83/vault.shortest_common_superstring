set(pointer-chasing-pass-regex ".*usage of pointer-chasing data structure detected.*")

set(pointer-chasing-test-template [=[
#include ${header}

using placeholder = ${type};

int main() {
  placeholder { };
}
]=])  

set(use-view-types-pass-regex ".*passing standard container by const reference.*")

set(use-view-types-test-template [=[
#include ${header}

void fn(${type} const &arg);
]=])  

set(clang-tidy-test-cases
  "pointer-chasing"  "std-set"            "<set>"            "std::set<int>"
  "pointer-chasing"  "std-map"            "<map>"            "std::map<int, int>"
  "pointer-chasing"  "std-list"           "<list>"           "std::list<int>"
  "pointer-chasing"  "std-forward-list"   "<forward_list>"   "std::forward_list<int>"
  "pointer-chasing"  "std-unordered-map"  "<unordered_map>"  "std::unordered_map<int, int>"
  "pointer-chasing"  "std-unordered-set"  "<unordered_set>"  "std::unordered_set<int>"

  "use-view-types" "std-string" "<string>" "std::string"
  "use-view-types" "std-vector" "<vector>" "std::vector<int>"
)

while(clang-tidy-test-cases)
  list(POP_FRONT clang-tidy-test-cases kind case header type)

  string(CONFIGURE "${${kind}-test-template}"
    ${kind}-test-content
  )
  
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${kind}.${case}.cpp"
    "${${kind}-test-content}"
  )

  add_test(NAME vault-tidy.${kind}.${case} COMMAND
    clang-tidy-20
    -load=$<TARGET_FILE:vault-tidy-plugin>
    -checks=-*,vault-*
    -warnings-as-errors=*
    "${CMAKE_CURRENT_BINARY_DIR}/${kind}.${case}.cpp"
    --
    -std=c++23
  )

  set_tests_properties(vault-tidy.${kind}.${case} PROPERTIES
    PASS_REGULAR_EXPRESSION "${${kind}-pass-regex}"
  )
endwhile()
