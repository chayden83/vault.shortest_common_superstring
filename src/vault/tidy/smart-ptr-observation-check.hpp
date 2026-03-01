#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Smart Pointer Observation Check
  ///
  /// Flags smart pointers passed by const reference, enforcing the core
  /// guideline that functions should take T* or T const& for mere observation.
  class smart_ptr_observation_check : public clang::tidy::ClangTidyCheck {
  public:
    smart_ptr_observation_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
