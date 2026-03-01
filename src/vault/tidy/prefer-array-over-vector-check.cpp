#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "prefer-array-over-vector-check.hpp"

namespace custom_tidy_checks {

  prefer_array_over_vector_check::prefer_array_over_vector_check(
    llvm::StringRef                name,
    clang::tidy::ClangTidyContext* context
  )
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool
  prefer_array_over_vector_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus11;
  }

  void prefer_array_over_vector_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    auto const vector_type_matcher =
      hasCanonicalType(qualType(hasDeclaration(cxxRecordDecl(hasName("::std::vector")))));

    // Matches local variables initialized with integer literals or init lists
    auto const vector_decl_matcher =
      varDecl(
        hasLocalStorage(),
        hasType(vector_type_matcher),
        hasInitializer(anyOf(cxxConstructExpr(hasArgument(0, integerLiteral())), initListExpr()))
      )
        .bind("vector_var");

    finder->addMatcher(vector_decl_matcher, this);
  }

  void prefer_array_over_vector_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* var_decl = static_cast<clang::VarDecl const*>(result.Nodes.getNodeAs<clang::VarDecl>("vector_var"));
    if (var_decl == nullptr) {
      return;
    }

    auto const source_location = clang::SourceLocation{var_decl->getBeginLoc()};
    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    diag(
      source_location,
      "std::vector initialized with a constant size; prefer std::array to avoid heap allocation overhead"
    );
  }

} // namespace custom_tidy_checks
