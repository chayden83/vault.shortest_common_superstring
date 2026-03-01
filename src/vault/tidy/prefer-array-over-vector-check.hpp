#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Prefer Array Over Vector Check
  ///
  /// Flags local std::vector declarations initialized with a strictly
  /// compile-time constant size, enforcing the use of std::array to
  /// eliminate unnecessary heap allocations.
  class prefer_array_over_vector_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] prefer_array_over_vector_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
