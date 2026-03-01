#include <algorithm>
#include <cassert>

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "require-mut-tag-check.hpp"

namespace custom_tidy_checks {

  require_mut_tag_check::require_mut_tag_check(llvm::StringRef name, clang::tidy::ClangTidyContext* context)
    : clang::tidy::ClangTidyCheck(name, context)
    , exempted_types_raw_{Options.get("ExemptedTypes", "std::basic_ostream;std::basic_istream")} {
    auto split_start = size_t{0};
    auto split_end   = exempted_types_raw_.find(';');

    while (split_end != std::string::npos) {
      exempted_types_.push_back(exempted_types_raw_.substr(split_start, split_end - split_start));
      split_start = split_end + 1;
      split_end   = exempted_types_raw_.find(';', split_start);
    }
    exempted_types_.push_back(exempted_types_raw_.substr(split_start));
  }

  void require_mut_tag_check::storeOptions(clang::tidy::ClangTidyOptions::OptionMap& opts) {
    Options.store(opts, "ExemptedTypes", exempted_types_raw_);
  }

  [[nodiscard]] bool require_mut_tag_check::isLanguageVersionSupported(clang::LangOptions const& lang_opts) const {
    return lang_opts.CPlusPlus11;
  }

  void require_mut_tag_check::registerMatchers(clang::ast_matchers::MatchFinder* finder) {
    assert(finder != nullptr && "MatchFinder must not be null");

    using namespace clang::ast_matchers;

    auto const call_matcher = callExpr(callee(functionDecl())).bind("func_call");

    finder->addMatcher(call_matcher, this);
  }

  void require_mut_tag_check::check(clang::ast_matchers::MatchFinder::MatchResult const& result) {
    assert(result.Context != nullptr && "ASTContext must not be null");
    assert(result.SourceManager != nullptr && "SourceManager must not be null");

    auto const* call = static_cast<clang::CallExpr const*>(result.Nodes.getNodeAs<clang::CallExpr>("func_call"));
    if (call == nullptr) {
      return;
    }

    auto const* callee = static_cast<clang::FunctionDecl const*>(call->getDirectCallee());
    if (callee == nullptr) {
      return;
    }

    auto const target_name = callee->getQualifiedNameAsString();
    if (target_name == "vault::mut" || target_name == "abyss::mut") {
      return;
    }

    // Exclude overloaded operators (e.g., operator<<, operator=)
    // BUT explicitly evaluate operator() so lambdas and functors are checked.
    if (callee->isOverloadedOperator() && callee->getOverloadedOperator() != clang::OO_Call) {
      return;
    }

    auto const num_args   = static_cast<unsigned>(call->getNumArgs());
    auto const num_params = static_cast<unsigned>(callee->getNumParams());

    for (auto i = 0U; i < num_args; ++i) {
      if (i >= num_params) {
        break;
      }

      auto const* param = static_cast<clang::ParmVarDecl const*>(callee->getParamDecl(i));
      if (param == nullptr) {
        continue;
      }

      auto const param_type = param->getType();

      if (!param_type->isLValueReferenceType()) {
        continue;
      }

      // --- 1. DEFAULT ARGUMENT GUARD ---
      auto const* raw_arg = call->getArg(i);
      if (llvm::isa<clang::CXXDefaultArgExpr>(raw_arg)) {
        continue;
      }

      // --- 2. PERFECT FORWARDING GUARD ---
      if (auto const* pattern = callee->getTemplateInstantiationPattern()) {
        if (i < pattern->getNumParams()) {
          auto const* pattern_param = static_cast<clang::ParmVarDecl const*>(pattern->getParamDecl(i));
          if (pattern_param->getType()->isRValueReferenceType()) {
            continue;
          }
        }
      }

      // --- 3. EXPLICIT ALIAS DESUGARING ---
      auto const pointee_type      = param_type.getNonReferenceType();
      auto const canonical_pointee = pointee_type.getCanonicalType();

      if (canonical_pointee.isConstQualified()) {
        continue;
      }

      auto const* record_decl = static_cast<clang::CXXRecordDecl const*>(canonical_pointee->getAsCXXRecordDecl());
      if (record_decl != nullptr) {

        // --- C++23 RECURSIVE BASE CLASS SEARCH ---
        auto const is_exempt = [&](this auto const& self, clang::CXXRecordDecl const* record) -> bool {
          if (record == nullptr) {
            return false;
          }

          auto const exact_name = record->getQualifiedNameAsString();
          if (std::ranges::find(exempted_types_, exact_name) != exempted_types_.end()) {
            return true;
          }

          if (auto const* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
            if (auto const* template_decl =
                  static_cast<clang::ClassTemplateDecl const*>(spec->getSpecializedTemplate())) {
              auto const template_name = template_decl->getQualifiedNameAsString();
              if (std::ranges::find(exempted_types_, template_name) != exempted_types_.end()) {
                return true;
              }
            }
          }

          if (!record->hasDefinition()) {
            return false;
          }

          auto const* definition = static_cast<clang::CXXRecordDecl const*>(record->getDefinition());
          for (auto const& base : definition->bases()) {
            auto const  canonical_base = base.getType().getCanonicalType();
            auto const* base_record    = static_cast<clang::CXXRecordDecl const*>(canonical_base->getAsCXXRecordDecl());

            if (self(base_record)) {
              return true;
            }
          }

          return false;
        };

        if (is_exempt(record_decl)) {
          continue;
        }
      }

      auto const* arg         = static_cast<clang::Expr const*>(raw_arg->IgnoreParenImpCasts());
      auto        has_mut_tag = false;

      if (auto const* arg_call = llvm::dyn_cast<clang::CallExpr>(arg)) {
        if (auto const* arg_callee = static_cast<clang::FunctionDecl const*>(arg_call->getDirectCallee())) {
          auto const callee_name = arg_callee->getQualifiedNameAsString();
          if (callee_name == "vault::mut" || callee_name == "abyss::mut") {
            has_mut_tag = true;
          }
        }
      }

      if (!has_mut_tag) {
        auto const source_location = arg->getBeginLoc();
        if (!result.SourceManager->isInSystemHeader(source_location)) {
          diag(
            source_location,
            "argument bound to a non-const lvalue reference parameter must be explicitly wrapped in mut() at the call "
            "site to clarify mutation semantics"
          );
        }
      }
    }
  }

} // namespace custom_tidy_checks
