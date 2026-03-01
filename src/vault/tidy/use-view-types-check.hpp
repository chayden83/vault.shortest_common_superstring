#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Use View Types Check
  ///
  /// Flags function parameters passed as const references to std::vector or
  /// std::basic_string, recommending std::span or std::basic_string_view.
  class use_view_types_check : public clang::tidy::ClangTidyCheck {
  public:
    use_view_types_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
