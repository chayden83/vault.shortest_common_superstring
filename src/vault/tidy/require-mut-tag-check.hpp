#pragma once

#include <string>
#include <vector>

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Require Mut Tag Check
  ///
  /// Enforces call-site readability by requiring any argument bound to a
  /// non-const lvalue reference parameter to be explicitly wrapped in vault::mut().
  /// Exemptions for third-party or standard library types can be configured
  /// via the ExemptedTypes option in the .clang-tidy file.
  class require_mut_tag_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] require_mut_tag_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;
    void storeOptions(clang::tidy::ClangTidyOptions::OptionMap& opts) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;

  private:
    std::string const        exempted_types_raw_;
    std::vector<std::string> exempted_types_;
  };

} // namespace custom_tidy_checks
