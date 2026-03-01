#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "require-mut-tag-check.hpp"

namespace custom_tidy_checks {

  require_mut_tag_check::require_mut_tag_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context)
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool require_mut_tag_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus11;
  }

  void require_mut_tag_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    auto const call_matcher = callExpr(callee(functionDecl())).bind("func_call");

    finder->addMatcher(call_matcher, this);
  }

  void require_mut_tag_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    // Rely on implicit type deduction for the pointer return type.
    auto const* call = result.Nodes.getNodeAs<clang::CallExpr>("func_call");
    if (call == nullptr) {
      return;
    }

    auto const* callee = call->getDirectCallee();
    if (callee == nullptr) {
      return;
    }

    // Short-circuit to prevent infinite recursion on mut() itself.
    auto const target_name = callee->getQualifiedNameAsString();
    if (target_name == "vault::mut" || target_name == "abyss::mut") {
      return;
    }

    // Exclude overloaded operators (e.g., operator<<, operator=).
    if (callee->isOverloadedOperator()) {
      return;
    }

    auto const num_args   = static_cast<unsigned>(call->getNumArgs());
    auto const num_params = static_cast<unsigned>(callee->getNumParams());

    for (auto i = 0U; i < num_args; ++i) {
      if (i >= num_params) {
        break;
      }

      auto const* param = callee->getParamDecl(i);
      if (param == nullptr) {
        continue;
      }

      auto const param_type = param->getType();

      // 1. Check if the parameter is an lvalue reference.
      if (!param_type->isLValueReferenceType()) {
        continue;
      }

      // 2. Check if the underlying type is non-const.
      auto const pointee_type = param_type.getNonReferenceType();
      if (pointee_type.isConstQualified()) {
        continue;
      }

      // 3. Exclude standard stream parameters.
      auto const* record_decl = pointee_type->getAsCXXRecordDecl();
      if (record_decl != nullptr) {
        auto const name = record_decl->getQualifiedNameAsString();
        if (name == "std::basic_ostream" || name == "std::basic_istream") {
          continue;
        }
      }

      auto const* arg         = call->getArg(i)->IgnoreParenImpCasts();
      auto        has_mut_tag = false;

      if (auto const* arg_call = llvm::dyn_cast<clang::CallExpr>(arg)) {
        if (auto const* arg_callee = arg_call->getDirectCallee()) {
          auto const callee_name = arg_callee->getQualifiedNameAsString();
          if (callee_name == "vault::mut" || callee_name == "abyss::mut") {
            has_mut_tag = true;
          }
        }
      }

      if (!has_mut_tag) {
        auto const source_location = arg->getBeginLoc();
        if (!result.SourceManager->isInSystemHeader(source_location)) {
          diag(
            source_location,
            "argument bound to a non-const lvalue reference parameter must be explicitly wrapped in mut() at the call "
            "site to clarify mutation semantics"
          );
        }
      }
    }
  }

} // namespace custom_tidy_checks
