#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Multiple Boolean Parameters Check
  ///
  /// Flags functions that declare more than one boolean parameter.
  /// Enforces the use of strongly-typed enumerations to prevent the "Boolean Trap"
  /// and drastically improve call-site readability. Automatically resolves type
  /// aliases to their underlying canonical types.
  class multiple_bool_parameters_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] multiple_bool_parameters_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
