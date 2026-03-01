#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "smart-ptr-observation-check.hpp"

namespace custom_tidy_checks {

  smart_ptr_observation_check::smart_ptr_observation_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context)
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool
  smart_ptr_observation_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus11;
  }

  void smart_ptr_observation_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    auto const smart_ptr_matcher =
      parmVarDecl(hasType(references(qualType(
                    isConstQualified(),
                    hasDeclaration(cxxRecordDecl(hasAnyName("::std::unique_ptr", "::std::shared_ptr")))
                  ))))
        .bind("smart_ptr_param");

    finder->addMatcher(smart_ptr_matcher, this);
  }

  void smart_ptr_observation_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* matched_decl =
      static_cast<clang::ParmVarDecl const*>(result.Nodes.getNodeAs<clang::ParmVarDecl>("smart_ptr_param"));

    if (matched_decl == nullptr) {
      return;
    }

    auto const source_location = clang::SourceLocation{matched_decl->getBeginLoc()};

    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    diag(
      source_location,
      "passing smart pointer by const reference; prefer passing by value to manipulate ownership, or pass the "
      "underlying object by raw pointer/reference for observation"
    );
  }

} // namespace custom_tidy_checks
