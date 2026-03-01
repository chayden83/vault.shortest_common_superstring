#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Require Mut Tag Check
  ///
  /// Enforces call-site readability by requiring arguments passed to mutating
  /// parameters (decorated with abyss::out<T> or abyss::inout<T>) to be
  /// explicitly wrapped in vault::mut().
  class require_mut_tag_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] require_mut_tag_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
