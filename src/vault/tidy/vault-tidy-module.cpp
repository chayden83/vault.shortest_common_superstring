#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

#include "pointer-chasing-type-check.hpp"
#include "use-view-types-check.hpp"
#include "smart-ptr-observation-check.hpp"
#include "pass-by-small-value-check.hpp"
#include "callable-observation-check.hpp"
#include "prefer-array-over-vector-check.hpp"
#include "multiple-bool-parameters-check.hpp"

namespace custom_tidy_checks {

  /// # Vault Tidy Module
  ///
  /// A custom Clang-Tidy module registry for the "vault-" check category.
  /// This class acts as a factory, allowing for the easy addition of future
  /// custom checks to this module without modifying the LLVM source tree.
  class vault_tidy_module : public clang::tidy::ClangTidyModule {
  public:
    /// Registers all checks belonging to the vault module.
    ///
    /// @param check_factories The factory registry to append checks to.
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories& check_factories) override {
      // Register the pointer chasing check under the "vault-" prefix.
      // Future checks can be added here by duplicating this call with new types.
      check_factories.registerCheck<pointer_chasing_type_check>("vault-pointer-chasing-type");
      check_factories.registerCheck<use_view_types_check>("vault-use-view-types");
      check_factories.registerCheck<pass_small_by_value_check>("vault-pass-by-small-value");
      check_factories.registerCheck<callable_observation_check>("vault-callable-observation");
      check_factories.registerCheck<prefer_array_over_vector_check>("vault-prefer-array-over-vector");
      check_factories.registerCheck<multiple_bool_parameters_check>("vault-multiple-bool-parameters");
      check_factories.registerCheck<smart_ptr_observation_check>("vault-smart-pointer-observation");
    }
  };
} // namespace custom_tidy_checks

namespace {

  // Register the module with Clang-Tidy's internal plugin system.
  // This relies on LLVM's macro-based global static initialization.
  auto const vault_module_registration =
    clang::tidy::ClangTidyModuleRegistry::Add<custom_tidy_checks::vault_tidy_module>{
      "vault-module",
      "Adds custom checks under the vault- prefix."
    };

} // namespace
