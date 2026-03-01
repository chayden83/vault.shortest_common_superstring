#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Require Out/InOut Decorators Check
  ///
  /// Flags non-const lvalue reference parameters that are not explicitly
  /// decorated with vault::out<T> or vault::inout<T>. This enforces API
  /// clarity for mutating parameters without sacrificing reference performance.
  class require_out_inout_decorators_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] require_out_inout_decorators_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
