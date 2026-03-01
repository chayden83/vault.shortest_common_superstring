#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Pass Small By Value Check
  ///
  /// Identifies function parameters passed by constant reference where the
  /// underlying type is trivially copyable and 16 bytes or smaller.
  /// Suggests passing by value to eliminate pointer indirection overhead
  /// and improve CPU register utilization.
  class pass_small_by_value_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] pass_small_by_value_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
