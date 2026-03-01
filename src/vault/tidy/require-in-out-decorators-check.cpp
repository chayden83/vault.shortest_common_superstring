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

    if (!param->getType()->isLValueReferenceType()) {
      return;
    }

    auto const source_location = clang::SourceLocation{param->getBeginLoc()};
    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

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

    auto const  pointee_type = clang::QualType{param->getType().getNonReferenceType()};
    auto const* record_decl  = static_cast<clang::CXXRecordDecl const*>(pointee_type->getAsCXXRecordDecl());

    if (record_decl != nullptr) {
      auto const name = std::string{record_decl->getQualifiedNameAsString()};
      if (name == "std::basic_ostream" || name == "std::basic_istream") {
        return;
      }
    }

    // --- ROBUST TYPE INSPECTION ---
    // Functionally peel away type sugar layers to evaluate the semantic,
    // fully-qualified name known to the compiler.
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
        break; // No more sugar to peel
      }
      current_type = desugared_type;
    }

    if (has_decorator) {
      return;
    }

    diag(
      source_location,
      "non-const lvalue reference parameter must be explicitly decorated with vault::out<T> or vault::inout<T> to "
      "clarify mutation semantics at the API boundary"
    );
  }

} // namespace custom_tidy_checks
