#include <cassert>
#include <string>
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

    auto const* call = static_cast<clang::CallExpr const*>(result.Nodes.getNodeAs<clang::CallExpr>("func_call"));
    if (call == nullptr) {
      return;
    }

    auto const* callee = static_cast<clang::FunctionDecl const*>(call->getDirectCallee());
    if (callee == nullptr) {
      return;
    }

    // --- BREAK THE INFINITE LOOP ---
    // If the function being called is mut itself, short-circuit immediately.
    // getQualifiedNameAsString() reliably resolves ADL and omitted namespaces.
    auto const target_name = std::string{callee->getQualifiedNameAsString()};
    if (target_name == "vault::mut") {
      return;
    }

    auto const num_args   = unsigned{call->getNumArgs()};
    auto const num_params = unsigned{callee->getNumParams()};

    for (auto i = unsigned{0}; i < num_args; ++i) {
      if (i >= num_params) {
        break;
      }

      auto const* param = static_cast<clang::ParmVarDecl const *>(callee->getParamDecl(i));
      if (param == nullptr) {
        continue;
      }

      // --- ROBUST TYPE INSPECTION ---
      auto has_decorator = bool{false};
      auto current_type  = clang::QualType{param->getType()};

      while (!current_type.isNull()) {
        if (auto const* typedef_type = current_type->getAs<clang::TypedefType>()) {
          auto const name = std::string{typedef_type->getDecl()->getQualifiedNameAsString()};
          if (name == "vault::out" || name == "vault::inout" || name == "abyss::out" || name == "abyss::inout") {
            has_decorator = true;
            break;
          }
        } else if (auto const* tst = current_type->getAs<clang::TemplateSpecializationType>()) {
          if (tst->isTypeAlias()) {
            auto const* template_decl = tst->getTemplateName().getAsTemplateDecl();
            if (template_decl != nullptr) {
              auto const name = std::string{template_decl->getQualifiedNameAsString()};
              if (name == "vault::out" || name == "vault::inout" || name == "abyss::out" || name == "abyss::inout") {
                has_decorator = true;
                break;
              }
            }
          }
        }

        auto const desugared_type = clang::QualType{current_type.getSingleStepDesugaredType(*result.Context)};
        if (desugared_type == current_type) {
          break;
        }
        current_type = desugared_type;
      }

      if (!has_decorator) {
        continue;
      }

      // --- ARGUMENT VALIDATION ---
      auto const* arg         = static_cast<clang::Expr const *>(call->getArg(i)->IgnoreParenImpCasts());
      auto        has_mut_tag = bool{false};

      if (auto const* arg_call = llvm::dyn_cast<clang::CallExpr>(arg)) {
        if (auto const* arg_callee = arg_call->getDirectCallee()) {
          auto const callee_name = std::string{arg_callee->getQualifiedNameAsString()};
          if (callee_name == "vault::mut" || callee_name == "abyss::mut") {
            has_mut_tag = true;
          }
        }
      }

      if (!has_mut_tag) {
        auto const source_location = clang::SourceLocation{arg->getBeginLoc()};
        if (!result.SourceManager->isInSystemHeader(source_location)) {
          diag(
            source_location,
            "argument bound to a mutating parameter must be explicitly wrapped in mut() at the call site to clarify "
            "mutation semantics"
          );
        }
      }
    }
  }

} // namespace custom_tidy_checks
