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

set(multiple-boolean-parameters-pass-regex ".*prefer strongly-typed enumerations.*")

set(multiple-boolean-parameters-test-template [=[
void fn(bool foo, bool bar);
]=])  

set(require-in-out-decorators-pass-regex ".*explicitly decorated.*")

set(require-in-out-decorators-test-template [=[
#include <string>

void fn(std::string &out);
]=])

set(require-mut-tag-pass-regex ".*bound to a mutating parameter.*")

set(require-mut-tag-test-template [=[
#include <string>

namespace vault {
  template<typename T>
  using inout = T&;

  template<typename T>
  [[nodiscard]] constexpr T &mut(inout<T> arg) noexcept {
    return arg;
  }
}

void fn(vault::inout<std::string> io);

int main() {
  using vault::mut;
  auto s = std::string { };
  fn(s);
}
]=])

set(require-borrowed-ptr-decorator-pass-regex ".*explicitly decorated.*")

set(require-borrowed-ptr-decorator-test-template [=[
#include <string>

void fn(std::string *out);
]=])

set(clang-tidy-test-cases
  "pointer-chasing"  "std-set"            "<set>"            "std::set<int>"
  "pointer-chasing"  "std-map"            "<map>"            "std::map<int, int>"
  "pointer-chasing"  "std-list"           "<list>"           "std::list<int>"
  "pointer-chasing"  "std-forward-list"   "<forward_list>"   "std::forward_list<int>"
  "pointer-chasing"  "std-unordered-map"  "<unordered_map>"  "std::unordered_map<int, int>"
  "pointer-chasing"  "std-unordered-set"  "<unordered_set>"  "std::unordered_set<int>"

  "use-view-types"  "std-string"  "<string>"  "std::string"
  "use-view-types"  "std-vector"  "<vector>"  "std::vector<int>"

  "multiple-boolean-parameters" "bools" "" ""

  "require-in-out-decorators"      "string" "" ""
  "require-mut-tag"                "string" "" ""
  "require-borrowed-ptr-decorator" "string" "" ""
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
