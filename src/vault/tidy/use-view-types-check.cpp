#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "use-view-types-check.hpp"

namespace custom_tidy_checks {

  use_view_types_check::use_view_types_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context)
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool use_view_types_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus20; // std::span requires C++20
  }

  void use_view_types_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    auto const view_target_matcher =
      parmVarDecl(hasType(references(qualType(
                    isConstQualified(),
                    hasDeclaration(cxxRecordDecl(hasAnyName("::std::basic_string", "::std::vector")))
                  ))))
        .bind("view_param");

    finder->addMatcher(view_target_matcher, this);
  }

  void use_view_types_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* matched_decl =
      static_cast<clang::ParmVarDecl const*>(result.Nodes.getNodeAs<clang::ParmVarDecl>("view_param"));

    if (matched_decl == nullptr) {
      return;
    }

    auto const source_location = clang::SourceLocation{matched_decl->getBeginLoc()};

    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    diag(
      source_location,
      "passing standard container by const reference; prefer std::span or std::basic_string_view to decouple from "
      "memory ownership and improve performance"
    );
  }

} // namespace custom_tidy_checks
