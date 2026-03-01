#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "pass-by-small-value-check.hpp"

namespace custom_tidy_checks {

  pass_small_by_value_check::pass_small_by_value_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context)
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool pass_small_by_value_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus;
  }

  void pass_small_by_value_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    auto const ref_param_matcher = parmVarDecl(hasType(references(qualType(isConstQualified())))).bind("small_param");

    finder->addMatcher(ref_param_matcher, this);
  }

  void pass_small_by_value_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* param =
      static_cast<clang::ParmVarDecl const*>(result.Nodes.getNodeAs<clang::ParmVarDecl>("small_param"));
    if (param == nullptr) {
      return;
    }

    auto const source_location = clang::SourceLocation{param->getBeginLoc()};
    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    auto const qual_type    = clang::QualType{param->getType()};
    auto const pointee_type = clang::QualType{qual_type->getPointeeType()};

    if (!pointee_type.isTriviallyCopyableType(*result.Context)) {
      return;
    }

    // getTypeSize returns the size in bits. 16 bytes = 128 bits.
    auto const size_in_bits = uint64_t{result.Context->getTypeSize(pointee_type)};
    if (size_in_bits <= 128) {
      diag(
        source_location,
        "small trivially copyable type passed by const reference; prefer passing by value to avoid pointer indirection"
      );
    }
  }

} // namespace custom_tidy_checks
