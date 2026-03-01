#include <cassert>
#include <string>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "require-in-out-decorators-check.hpp"

namespace custom_tidy_checks {

  require_out_inout_decorators_check::require_out_inout_decorators_check(
    llvm::StringRef                name,
    clang::tidy::ClangTidyContext* context
  )
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool
  require_out_inout_decorators_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus11;
  }

  void require_out_inout_decorators_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    // We use 'references' to match both lvalue and rvalue references to non-const types.
    // We will strictly filter for lvalue references inside the check() callback.
    auto const mutating_param_matcher =
      parmVarDecl(hasType(references(qualType(unless(isConstQualified()))))).bind("mutating_param");

    finder->addMatcher(mutating_param_matcher, this);
  }

  void require_out_inout_decorators_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* param =
      static_cast<clang::ParmVarDecl const*>(result.Nodes.getNodeAs<clang::ParmVarDecl>("mutating_param"));
    if (param == nullptr) {
      return;
    }

    // 1. Strictly verify it is an lvalue reference (T&) and not an rvalue reference (T&&)
    if (!param->getType()->isLValueReferenceType()) {
      return;
    }

    auto const source_location = clang::SourceLocation{param->getBeginLoc()};
    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    // 2. Evaluate the parent function context to ignore standard language boilerplate
    auto const* func = llvm::dyn_cast_or_null<clang::FunctionDecl>(param->getDeclContext());
    if (func != nullptr) {
      if (func->isImplicit() || func->isOverloadedOperator() || func->isMain()) {
        return;
      }

      if (auto const* method = llvm::dyn_cast<clang::CXXMethodDecl>(func)) {
        if (method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator()) {
          return;
        }
      }

      if (auto const* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(func)) {
        if (ctor->isCopyOrMoveConstructor()) {
          return;
        }
      }
    }

    // 3. Ignore standard stream objects which conventionally use non-const references
    auto const  pointee_type = clang::QualType{param->getType().getNonReferenceType()};
    auto const* record_decl  = static_cast<clang::CXXRecordDecl const*>(pointee_type->getAsCXXRecordDecl());

    if (record_decl != nullptr) {
      auto const name = std::string{record_decl->getQualifiedNameAsString()};
      if (name == "std::basic_ostream" || name == "std::basic_istream") {
        return;
      }
    }

    // 4. Retrieve the type exactly as written in the source code to verify decorators
    auto const original_type_str =
      std::string{param->getOriginalType().getAsString(result.Context->getPrintingPolicy())};

    if (original_type_str.find("vault::out") != std::string::npos ||
        original_type_str.find("vault::inout") != std::string::npos) {
      return;
    }

    diag(
      source_location,
      "non-const lvalue reference parameter must be explicitly decorated with vault::out<T> or vault::inout<T> to "
      "clarify mutation semantics at the API boundary"
    );
  }

} // namespace custom_tidy_checks
