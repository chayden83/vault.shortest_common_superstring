#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "callable-observation-check.hpp"

namespace custom_tidy_checks {

  callable_observation_check::callable_observation_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context)
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool callable_observation_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus11;
  }

  void callable_observation_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    auto const std_function_matcher = hasCanonicalType(
      qualType(hasDeclaration(cxxRecordDecl(hasAnyName("::std::function", "::fu2::function", "::boost::function"))))
    );

    auto const param_matcher =
      parmVarDecl(hasType(references(qualType(isConstQualified(), std_function_matcher)))).bind("func_param");

    auto const return_matcher =
      functionDecl(returns(references(qualType(isConstQualified(), std_function_matcher)))).bind("func_return");

    finder->addMatcher(param_matcher, this);
    finder->addMatcher(return_matcher, this);
  }

  void callable_observation_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* param_decl =
      static_cast<clang::ParmVarDecl const*>(result.Nodes.getNodeAs<clang::ParmVarDecl>("func_param"));
    auto const* return_decl =
      static_cast<clang::FunctionDecl const*>(result.Nodes.getNodeAs<clang::FunctionDecl>("func_return"));

    auto const source_location =
      clang::SourceLocation{param_decl != nullptr ? param_decl->getBeginLoc() : return_decl->getBeginLoc()};

    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    if (param_decl != nullptr) {
      diag(
        source_location,
        "std::function passed by const reference; prefer a view type like boost::function_ref to avoid implicit "
        "allocations on the hot path"
      );
    } else if (return_decl != nullptr) {
      diag(
        source_location,
        "std::function returned by const reference; prefer returning a view type like boost::function_ref to decouple "
        "storage from invocation"
      );
    }
  }

} // namespace custom_tidy_checks
