#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Callable Observation Check
  ///
  /// Flags the usage of std::function when passed as a const reference
  /// parameter or returned by reference. Enforces the use of non-owning
  /// view types (e.g., boost::function_ref) for synchronous invocation
  /// to avoid implicit heap allocations.
  class callable_observation_check : public clang::tidy::ClangTidyCheck {
  public:
    [[nodiscard]] callable_observation_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks
