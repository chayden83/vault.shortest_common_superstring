#include <algorithm>
#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "multiple-bool-parameters-check.hpp"

namespace custom_tidy_checks {

  multiple_bool_parameters_check::multiple_bool_parameters_check(
    llvm::StringRef                name,
    clang::tidy::ClangTidyContext* context
  )
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool
  multiple_bool_parameters_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus;
  }

  void multiple_bool_parameters_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    // Wrap isOverride() inside cxxMethodDecl() to safely cast the AST node.
    // This allows the matcher to gracefully ignore free functions while
    // correctly evaluating virtual class methods.
    auto const func_matcher =
      functionDecl(unless(isImplicit()), unless(isInstantiated()), unless(cxxMethodDecl(isOverride())))
        .bind("func_decl");

    finder->addMatcher(func_matcher, this);
  }

  void multiple_bool_parameters_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* func =
      static_cast<clang::FunctionDecl const*>(result.Nodes.getNodeAs<clang::FunctionDecl>("func_decl"));
    if (func == nullptr) {
      return;
    }

    auto const source_location = clang::SourceLocation{func->getBeginLoc()};
    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    auto const bool_count = std::ranges::count_if(func->parameters(), [](clang::ParmVarDecl const* param) {
      if (param == nullptr) {
        return false;
      }
      auto const qual_type = clang::QualType{param->getType()};
      return qual_type.getCanonicalType()->isBooleanType();
    });

    if (bool_count > 1) {
      diag(
        func->getLocation(),
        "function has %0 boolean parameters; limit to 1 and prefer strongly-typed enumerations to prevent call-site "
        "confusion"
      ) << static_cast<unsigned>(bool_count);
    }
  }

} // namespace custom_tidy_checks
