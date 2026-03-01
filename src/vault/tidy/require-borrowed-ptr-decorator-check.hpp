#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Require Borrowed Pointer Decorator Check
  ///
  /// Flags raw pointer parameters that are not explicitly decorated with
  /// vault::borrowed_ptr<T>. This enforces API clarity, explicitly denoting
  /// non-owning, nullable observation without sacrificing pointer performance.
  class require_borrowed_ptr_decorator_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] require_borrowed_ptr_decorator_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
