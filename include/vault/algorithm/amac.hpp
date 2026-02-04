// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef VAULT_AMAC_HPP
#define VAULT_AMAC_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// clang-format off

/**
 * @defgroup vault_amac Asynchronous Memory Access Coordinator (AMAC)
 * @brief A composable, exception-safe software pipelining engine.
 */

namespace vault::amac::concepts {

  template <typename T>
  concept step_result = std::constructible_from<bool, T> &&
    []<std::size_t... Is>(std::index_sequence<Is...>) {
      return (std::same_as<void const*, std::tuple_element_t<Is, T>> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});

  template <typename J>
  concept job = std::movable<J>;

  /**
   * @brief Context concept.
   * A Context defines the behavior for a Job. It must support `init` and `step`
   * methods that accept a generic "emit" callback for dynamic job creation.
   */
  template <typename C, typename J>
  concept context = job<J> && requires(C& ctx, J& job) {
    { C::fanout() } -> std::convertible_to<std::size_t>;
    { ctx.init(job, [](J&&){}) } -> step_result;
    { ctx.step(job, [](J&&){}) } -> step_result;
  };

  /**
   * @brief Reporter concept.
   * Handles the terminal state of a job (Completion or Failure).
   */
  template <typename R, typename J>
  concept reporter = job<J> && requires(R& r, J&& job, std::exception_ptr e) {
    { r.on_completion(std::move(job)) };
    { r.on_failure(std::move(job), e) };
  };

} // namespace vault::amac::concepts

namespace vault::amac {

  enum class double_fault_policy {
    rethrow,   ///< Propagate exception (aborts batch, destroys active jobs).
    suppress,  ///< Catch and ignore exception (orphans the failing job).
    terminate  ///< Call std::terminate immediately.
  };

  /**
   * @brief Fixed-size collection of memory addresses to prefetch.
   */
  template <std::size_t N>
  struct step_result : public std::array<void const*, N> {
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
      return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (((*this)[Is] != nullptr) || ...);
      }(std::make_index_sequence<N>{});
    }
  };

  // --- Implementation Details ---

  namespace detail {
      // Promotes a step_result<Small> to step_result<Big> via zero-padding.
      template <std::size_t Big, std::size_t Small>
      constexpr step_result<Big> lift_result(step_result<Small> const& src) {
          static_assert(Big >= Small, "Cannot lift to a smaller fanout");
          step_result<Big> dst{}; // Zero-initialize (nullptrs)
          for(std::size_t i = 0; i < Small; ++i) {
              dst[i] = src[i];
          }
          return dst;
      }

      // Generates a switch-like jump table for runtime-index -> compile-time-template dispatch.
      template <std::size_t N, typename Op>
      constexpr decltype(auto) dispatch_index(std::size_t i, Op&& op) {
          return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
              using ResultT = decltype(op.template operator()<0>());
              static constexpr ResultT(*table[])(Op&&) = {
                  [](Op&& o) { return o.template operator()<Is>(); }...
              };
              return table[i](std::forward<Op>(op));
          }(std::make_index_sequence<N>{});
      }
  }

  // --- Composition / Pipeline ---

  /**
   * @brief A composite context that executes a sequence of contexts.
   * Flattens nested chains into a single std::variant state machine.
   */
  template <typename ContextTuple, typename TransitionTuple>
  class pipeline_context {
      [[no_unique_address]] ContextTuple m_contexts;
      [[no_unique_address]] TransitionTuple m_transitions;

      template <typename Tuple> struct tuple_to_variant;
      template <typename... Cs> 
      struct tuple_to_variant<std::tuple<Cs...>> {
          using type = std::variant<typename Cs::job_t...>;
      };

  public:
      using job_t = typename tuple_to_variant<ContextTuple>::type;

      static constexpr std::size_t fanout() {
          return std::apply([](auto const&... ctxs) {
              return std::max({ctxs.fanout()...});
          }, ContextTuple{});
      }
      
      using result_t = step_result<fanout()>;
      static constexpr std::size_t num_stages = std::tuple_size_v<ContextTuple>;

      pipeline_context(ContextTuple ctxs, TransitionTuple trans)
          : m_contexts(std::move(ctxs))
          , m_transitions(std::move(trans)) 
      {}

      template <typename Emit>
      result_t init(job_t& j, Emit&& emit) {
          // O(1) dispatch to the correct sub-context's init
          return detail::dispatch_index<num_stages>(j.index(), [&]<std::size_t I>() {
              return detail::lift_result<fanout()>(
                  std::get<I>(m_contexts).init(std::get<I>(j), emit)
              );
          });
      }

      template <typename Emit>
      result_t step(job_t& j, Emit&& emit) {
          return detail::dispatch_index<num_stages>(j.index(), [&]<std::size_t I>() {
              auto& ctx = std::get<I>(m_contexts);
              auto& job = std::get<I>(j);
              
              // 1. Run current stage
              auto res = ctx.step(job, emit);
              
              // If active, continue (lift result to match max fanout)
              if (static_cast<bool>(res)) {
                  return detail::lift_result<fanout()>(res);
              }

              // 2. Stage finished. Check for transition to Next Stage (I -> I+1).
              if constexpr (I < std::tuple_size_v<TransitionTuple>) {
                  auto& transition_fn = std::get<I>(m_transitions);
                  
                  if (auto next_job = transition_fn(std::move(job))) {
                      // Construct next state in-place
                      j.template emplace<I+1>(std::move(*next_job));
                      
                      // Recursively init the next stage to fill the pipeline bubble
                      return detail::lift_result<fanout()>(
                          std::get<I+1>(m_contexts).init(std::get<I+1>(j), emit)
                      );
                  }
              }
              
              // No transition possible or requested -> Job Complete.
              return detail::lift_result<fanout()>(res);
          });
      }
      
      // Accessors for flattening logic
      auto& contexts() && { return m_contexts; }
      auto& transitions() && { return m_transitions; }
  };

  template <typename T> struct is_pipeline : std::false_type {};
  template <typename C, typename T> 
  struct is_pipeline<pipeline_context<C, T>> : std::true_type {};

  /**
   * @brief Factory to chain contexts. 
   * Flattens nested chains into a single linear pipeline context.
   */
  template <typename Left, typename Right, typename TransitionFn>
  auto chain(Left left, Right right, TransitionFn trans) {
      if constexpr (is_pipeline<std::decay_t<Left>>::value) {
          // Optimization: Flatten (A, B) + C -> (A, B, C)
          auto combined_ctxs = std::tuple_cat(
              std::move(left).contexts(), 
              std::make_tuple(std::move(right))
          );
          auto combined_trans = std::tuple_cat(
              std::move(left).transitions(), 
              std::make_tuple(std::move(trans))
          );
          return pipeline_context(std::move(combined_ctxs), std::move(combined_trans));
      } else {
          // New Chain: A + B -> (A, B)
          auto ctx_tuple = std::make_tuple(std::move(left), std::move(right));
          auto trans_tuple = std::make_tuple(std::move(trans));
          return pipeline_context(std::move(ctx_tuple), std::move(trans_tuple));
      }
  }

  // --- Executor ---

  /**
   * @brief Executor implementation.
   * @tparam ProtoAllocator A prototype allocator (e.g. std::allocator<std::byte>) used to construct the backlog.
   */
  template <uint8_t TotalFanout = 16, 
            double_fault_policy FailurePolicy = double_fault_policy::terminate,
            typename ProtoAllocator = std::allocator<std::byte>>
  class executor_fn {
    
    // -- Safety Shim --
    // Encapsulates error handling policies to keep the main loop clean.
    template <typename Reporter>
    struct safety_shim {
        Reporter& report;

        void fail(auto&& job, std::exception_ptr e) {
            try {
                report.on_failure(std::move(job), e);
            } catch (...) {
                if constexpr (FailurePolicy == double_fault_policy::terminate) {
                    std::terminate();
                } else if constexpr (FailurePolicy == double_fault_policy::rethrow) {
                    throw;
                }
                // Suppress
            }
        }

        void complete(auto&& job) {
            try {
                report.on_completion(std::move(job));
            } catch (...) {
                fail(std::move(job), std::current_exception());
            }
        }

        template <typename Job, typename Action>
        bool try_execute(Job& job, Action&& action) {
            try {
                if (auto result = action()) {
                    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                        (__builtin_prefetch(std::get<Is>(result), 0, 3), ...);
                    }(std::make_index_sequence<std::tuple_size_v<decltype(result)>>{});
                    return true;
                } else {
                    complete(std::move(job));
                    return false;
                }
            } catch (...) {
                fail(std::move(job), std::current_exception());
                return false;
            }
        }
    };

    // -- Internal Storage --
    
    template <typename J>
    class alignas(J) job_slot {
      std::byte storage[sizeof(J)];

    public:
      using value_type = J;
      [[nodiscard]] job_slot() = default;
      job_slot(job_slot const&) = delete;
      job_slot& operator=(job_slot const&) = delete;

      // Invoked by std::remove_if during compaction
      job_slot& operator=(job_slot&& other) {
        if (this != std::addressof(other)) {
          *this->get() = std::move(*other.get());
        }
        return *this;
      }

      [[nodiscard]] J* get() noexcept {
        return reinterpret_cast<J*>(&storage[0]);
      }
    };

    /**
     * @brief RAII Guard. 
     * Ensures all initialized objects in [first, last) are destroyed if an exception escapes.
     */
    template<typename Iter>
    struct scope_guard {
        Iter& first;
        Iter const& last;
        
        ~scope_guard() {
            using JobType = typename std::iterator_traits<Iter>::value_type::value_type;
            if constexpr (!std::is_trivially_destructible_v<JobType>) {
                for (; first != last; ++first) {
                    std::destroy_at(first->get());
                }
            }
        }
    };

  public:
    template <typename Context,
              std::ranges::input_range Jobs,
              concepts::reporter<std::ranges::range_value_t<Jobs>> Reporter>
    requires concepts::context<Context, std::ranges::range_value_t<Jobs>>
    static constexpr void operator()(Context& ctx, Jobs&& ijobs, Reporter&& reporter, ProtoAllocator proto_alloc = ProtoAllocator())
    {
      using job_t = std::ranges::range_value_t<Jobs>;
      using slot_t = job_slot<job_t>;
      
      static constexpr auto const PIPELINE_SIZE =
        (TotalFanout + Context::fanout() - 1) / Context::fanout();
      using array_t = std::array<slot_t, PIPELINE_SIZE>;
      
      // -- Setup --
      auto [ijobs_cursor, ijobs_last] = std::ranges::subrange(ijobs);
      safety_shim<Reporter> safety{reporter};
      
      auto pipeline = array_t{};
      
      // Rebind the prototype allocator to the actual job type
      using JobAllocator = typename std::allocator_traits<ProtoAllocator>::template rebind_alloc<job_t>;
      std::vector<job_t, JobAllocator> backlog(proto_alloc);

      auto emit = [&](job_t&& spawned) {
          backlog.push_back(std::move(spawned));
      };

      // -- State Tracking --
      auto initialized_end = pipeline.begin();
      auto active_end = pipeline.begin();

      // Guard needs lvalue
      auto pipeline_begin = pipeline.begin();
      scope_guard guard{pipeline_begin, initialized_end};

      // -- Loop Helpers --
      
      auto activate_into_slot = [&](job_t&& job, auto slot) -> bool {
          if (safety.try_execute(job, [&]{ return ctx.init(job, emit); })) {
              if (slot < initialized_end) {
                  *slot->get() = std::move(job);
              } else {
                  std::construct_at(slot->get(), std::move(job));
                  ++initialized_end;
              }
              return true;
          }
          return false; 
      };

      auto is_inactive = [&](auto& slot) {
          return !safety.try_execute(*slot.get(), [&]{ return ctx.step(*slot.get(), emit); });
      };
      
      // -- Unified Control Loop --
      
      while (true) {
          // 1. Greedy Refill
          while (active_end != pipeline.end()) {
              if (!backlog.empty()) {
                   if (activate_into_slot(std::move(backlog.back()), active_end)) {
                       ++active_end;
                   }
                   backlog.pop_back();
              } 
              else if (ijobs_cursor != ijobs_last) {
                   if (activate_into_slot(std::move(*ijobs_cursor), active_end)) {
                       ++active_end;
                   }
                   ++ijobs_cursor;
              } 
              else {
                   break; // No work left
              }
          }

          // 2. Termination Check
          if (active_end == pipeline.begin()) {
              break;
          }

          // 3. Execution & Compaction
          active_end = std::remove_if(pipeline.begin(), active_end, is_inactive);
      }
    }
  };

  /**
   * @brief Global instance of the executor.
   */
  template <uint8_t TotalFanout = 16, 
            double_fault_policy FailurePolicy = double_fault_policy::terminate,
            typename Allocator = std::allocator<std::byte>>
  constexpr inline auto const executor = executor_fn<TotalFanout, FailurePolicy, Allocator>{};

} // namespace vault::amac

template <std::size_t N>
struct std::tuple_size<vault::amac::step_result<N>> {
  static constexpr inline auto const value = std::size_t{N};
};

template <std::size_t I, std::size_t N>
struct std::tuple_element<I, vault::amac::step_result<N>> {
  using type = void const*;
};

#endif
