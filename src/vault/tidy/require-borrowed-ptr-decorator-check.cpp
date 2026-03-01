#include <cassert>
#include <string>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "require-borrowed-ptr-decorator-check.hpp"

namespace custom_tidy_checks {

  require_borrowed_ptr_decorator_check::require_borrowed_ptr_decorator_check(
    llvm::StringRef                name,
    clang::tidy::ClangTidyContext* context
  )
    : clang::tidy::ClangTidyCheck(name, context) {}

  [[nodiscard]] bool
  require_borrowed_ptr_decorator_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus11;
  }

  void require_borrowed_ptr_decorator_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    // Match any parameter that decays to or is declared as a pointer type.
    auto const pointer_param_matcher = parmVarDecl(hasType(pointerType())).bind("ptr_param");

    finder->addMatcher(pointer_param_matcher, this);
  }

  void require_borrowed_ptr_decorator_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* param = static_cast<clang::ParmVarDecl const*>(result.Nodes.getNodeAs<clang::ParmVarDecl>("ptr_param"));
    if (param == nullptr) {
      return;
    }

    auto const source_location = clang::SourceLocation{param->getBeginLoc()};
    if (result.SourceManager->isInSystemHeader(source_location)) {
      return;
    }

    // 1. Exclude function pointers and member pointers. These have highly specific
    // syntaxes and do not represent data observation in the same way.
    auto const type = clang::QualType{param->getType()};
    if (type->isFunctionPointerType() || type->isMemberPointerType()) {
      return;
    }

    // 2. Evaluate the parent function context to ignore standard language boilerplate.
    auto const* func = llvm::dyn_cast_or_null<clang::FunctionDecl>(param->getDeclContext());
    if (func != nullptr) {
      if (func->isImplicit() || func->isMain()) {
        return;
      }
    }

    // 3. Retrieve the type exactly as written in the source code to verify decorators.
    auto const original_type_str =
      std::string{param->getOriginalType().getAsString(result.Context->getPrintingPolicy())};

    if (original_type_str.find("vault::borrowed_ptr") != std::string::npos) {
      return;
    }

    diag(
      source_location,
      "raw pointer parameter must be explicitly decorated with vault::borrowed_ptr<T> to clarify non-owning "
      "observation semantics at the API boundary"
    );
  }

} // namespace custom_tidy_checks
