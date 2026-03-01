#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "pointer-chasing-type-check.hpp"

namespace custom_tidy_checks {

  pointer_chasing_type_check::pointer_chasing_type_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context)
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool pointer_chasing_type_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus;
  }

  void pointer_chasing_type_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    // Match any TypeLoc that references one of the prohibited container types.
    // We bind it to the ID "pointer_chasing_type" for retrieval in the check callback.
    auto const prohibited_type_matcher = typeLoc(
        loc(qualType(hasDeclaration(cxxRecordDecl(hasAnyName(
            "::std::map",
            "::std::set",
            "::std::list",
            "::std::forward_list",
            "::std::unordered_map",
            "::std::unordered_set"
        )))))
    ).bind("pointer_chasing_type");

    finder->addMatcher(prohibited_type_matcher, this);
  }

  void pointer_chasing_type_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* matched_type_loc = result.Nodes.getNodeAs<clang::TypeLoc>("pointer_chasing_type");

    if (matched_type_loc == nullptr) {
      return;
    }

    auto const source_location = clang::SourceLocation{matched_type_loc->getBeginLoc()};

    // Prevent diagnostics from triggering on third-party code and standard library internal implementations.
    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    diag(
      source_location,
      "usage of pointer-chasing data structure detected; prefer contiguous data structures to avoid cache misses"
    );
  }

} // namespace custom_tidy_checks
