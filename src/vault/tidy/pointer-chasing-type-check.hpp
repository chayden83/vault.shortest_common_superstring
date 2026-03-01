#pragma once

#include <clang-tidy/ClangTidyCheck.h>

namespace custom_tidy_checks {

  /// # Pointer Chasing Type Check
  ///
  /// A Clang-Tidy check that identifies and flags the usage of standard library
  /// node-based data structures that rely on pointer chasing.
  ///
  /// This check targets all occurrences of the prohibited types, including
  /// variable declarations, return types, function parameters, and type aliases.
  /// It silently ignores matches found inside system headers.
  class pointer_chasing_type_check : public clang::tidy::ClangTidyCheck {
  public:
    /// Constructs the check.
    ///
    /// @param name The name of the check.
    /// @param context The Clang-Tidy context.
    pointer_chasing_type_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context);

    /// Registers the AST matchers for the prohibited types.
    ///
    /// @param finder The AST match finder.
    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override;

    /// Evaluates the matched AST nodes and emits warnings.
    ///
    /// @param result The match result containing the bound nodes.
    void check(clang::ast_matchers::MatchFinder::MatchResult const& result) override;

    /// Determines whether this check is language-specific.
    ///
    /// @param lang_opts The language options to check.
    /// @return True if the language is C++, false otherwise.
    [[nodiscard]] bool isLanguageVersionSupported(clang::LangOptions const& lang_opts) const override;
  };

} // namespace custom_tidy_checks

