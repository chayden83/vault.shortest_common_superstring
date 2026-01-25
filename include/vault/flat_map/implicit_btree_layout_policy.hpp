#ifndef IMPLICIT_BTREE_LAYOUT_POLICY_HPP
#define IMPLICIT_BTREE_LAYOUT_POLICY_HPP

#include <ranges>
#include <vector>
#include <concepts>
#include <iterator>
#include <algorithm>
#include <functional>

#if defined(__GNUC__) || defined(__clang__)
    #define LAYOUT_PREFETCH(ptr) __builtin_prefetch(ptr, 0, 3)
#elif defined(_MSC_VER)
    #include <intrin.h>
    #define LAYOUT_PREFETCH(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#else
    #define LAYOUT_PREFETCH(ptr)
#endif

#if defined(__AVX2__)
    #define LAYOUT_USE_AVX2 1
#endif

namespace eytzinger {

// --- Concepts ---

template <typename Comp, typename T>
concept IsStandardLess =
    std::same_as<Comp, std::less<T>> || std::same_as<Comp, std::less<void>> ||
    std::same_as<Comp, std::less<>> || std::same_as<Comp, std::ranges::less>;

template <typename Comp, typename T>
concept IsStandardGreater =
    std::same_as<Comp, std::greater<T>> || std::same_as<Comp, std::greater<void>> ||
    std::same_as<Comp, std::greater<>> || std::same_as<Comp, std::ranges::greater>;

// --- Internal Declarations (Implemented in .cpp) ---

namespace detail {
    // Topology / Math Helpers
    std::size_t btree_child_block_index(std::size_t block_idx, std::size_t child_slot, std::size_t B);
    std::size_t btree_sorted_rank_to_index(std::size_t rank, std::size_t n, std::size_t B);
    std::size_t btree_index_to_sorted_rank(std::size_t index, std::size_t n, std::size_t B);
    std::ptrdiff_t btree_next_index(std::ptrdiff_t i, std::size_t n, std::size_t B);
    std::ptrdiff_t btree_prev_index(std::ptrdiff_t i, std::size_t n, std::size_t B);

#ifdef LAYOUT_USE_AVX2
    // Fast Path Full-Loop Implementations
    std::size_t search_loop_lb_uint64_less(const uint64_t* base, std::size_t n, uint64_t key, std::size_t B);
    std::size_t search_loop_ub_uint64_less(const uint64_t* base, std::size_t n, uint64_t key, std::size_t B);
    std::size_t search_loop_lb_int64_less(const int64_t* base, std::size_t n, int64_t key, std::size_t B);
    std::size_t search_loop_ub_int64_less(const int64_t* base, std::size_t n, int64_t key, std::size_t B);

    // SIMD Block Searcher Trampolines
    std::size_t simd_lb_int64_less(const int64_t* b, int64_t k);    std::size_t simd_ub_int64_less(const int64_t* b, int64_t k);
    std::size_t simd_lb_uint64_less(const uint64_t* b, uint64_t k); std::size_t simd_ub_uint64_less(const uint64_t* b, uint64_t k);
    std::size_t simd_lb_int64_greater(const int64_t* b, int64_t k); std::size_t simd_ub_int64_greater(const int64_t* b, int64_t k);
    std::size_t simd_lb_uint64_greater(const uint64_t* b, uint64_t k); std::size_t simd_ub_uint64_greater(const uint64_t* b, uint64_t k);

    std::size_t simd_lb_int32_less(const int32_t* b, int32_t k);    std::size_t simd_ub_int32_less(const int32_t* b, int32_t k);
    std::size_t simd_lb_uint32_less(const uint32_t* b, uint32_t k); std::size_t simd_ub_uint32_less(const uint32_t* b, uint32_t k);
    std::size_t simd_lb_int32_greater(const int32_t* b, int32_t k); std::size_t simd_ub_int32_greater(const int32_t* b, int32_t k);
    std::size_t simd_lb_uint32_greater(const uint32_t* b, uint32_t k); std::size_t simd_ub_uint32_greater(const uint32_t* b, uint32_t k);

    std::size_t simd_lb_int16_less(const int16_t* b, int16_t k);    std::size_t simd_ub_int16_less(const int16_t* b, int16_t k);
    std::size_t simd_lb_uint16_less(const uint16_t* b, uint16_t k); std::size_t simd_ub_uint16_less(const uint16_t* b, uint16_t k);
    std::size_t simd_lb_int16_greater(const int16_t* b, int16_t k); std::size_t simd_ub_int16_greater(const int16_t* b, int16_t k);
    std::size_t simd_lb_uint16_greater(const uint16_t* b, uint16_t k); std::size_t simd_ub_uint16_greater(const uint16_t* b, uint16_t k);

    std::size_t simd_lb_int8_less(const int8_t* b, int8_t k);       std::size_t simd_ub_int8_less(const int8_t* b, int8_t k);
    std::size_t simd_lb_uint8_less(const uint8_t* b, uint8_t k);    std::size_t simd_ub_uint8_less(const uint8_t* b, uint8_t k);
    std::size_t simd_lb_int8_greater(const int8_t* b, int8_t k);    std::size_t simd_ub_int8_greater(const int8_t* b, int8_t k);
    std::size_t simd_lb_uint8_greater(const uint8_t* b, uint8_t k); std::size_t simd_ub_uint8_greater(const uint8_t* b, uint8_t k);
#endif
}

// --- Scalar Fallback Helper ---

struct scalar_block_searcher {
    template <typename T, std::size_t B, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t lower_bound(const T* block, const T& key, Comp comp, Proj proj) {
        std::size_t i = 0;
        for (; i < B; ++i) {
            if (!std::invoke(comp, std::invoke(proj, block[i]), key)) return i;
        }
        return B;
    }

    template <typename T, std::size_t B, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t upper_bound(const T* block, const T& key, Comp comp, Proj proj) {
        std::size_t i = 0;
        for (; i < B; ++i) {
            if (std::invoke(comp, key, std::invoke(proj, block[i]))) return i;
        }
        return B;
    }

    template <typename T, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t lower_bound_n(const T* block, std::size_t n, const T& key, Comp comp, Proj proj) {
        std::size_t i = 0;
        for (; i < n; ++i) {
            if (!std::invoke(comp, std::invoke(proj, block[i]), key)) return i;
        }
        return n;
    }

    template <typename T, typename Comp, typename Proj>
    [[nodiscard]] static constexpr std::size_t upper_bound_n(const T* block, std::size_t n, const T& key, Comp comp, Proj proj) {
        std::size_t i = 0;
        for (; i < n; ++i) {
            if (std::invoke(comp, key, std::invoke(proj, block[i]))) return i;
        }
        return n;
    }
};

// --- Block Searcher Primary Template ---

template <typename T, typename Comp, std::size_t B>
struct block_searcher {
    template <typename Proj>
    [[nodiscard]] static constexpr std::size_t lower_bound(const T* block, const T& key, Comp comp, Proj proj) {
        return scalar_block_searcher::lower_bound<T, B>(block, key, comp, proj);
    }

    template <typename Proj>
    [[nodiscard]] static constexpr std::size_t upper_bound(const T* block, const T& key, Comp comp, Proj proj) {
        return scalar_block_searcher::upper_bound<T, B>(block, key, comp, proj);
    }
};

#ifdef LAYOUT_USE_AVX2
#define DEFINE_SIMD_SPECIALIZATION(TYPE, BLOCK_SIZE, CONCEPT, SUFFIX) \
    template <typename Comp> \
    requires CONCEPT<Comp, TYPE> \
    struct block_searcher<TYPE, Comp, BLOCK_SIZE> { \
        template <typename Proj> \
        [[nodiscard]] static std::size_t lower_bound(const TYPE* block, const TYPE& key, Comp comp, Proj proj) { \
            if constexpr (std::is_same_v<Proj, std::identity>) { \
                return detail::simd_lb_##TYPE##_##SUFFIX(block, key); \
            } else { \
                return scalar_block_searcher::lower_bound<TYPE, BLOCK_SIZE>(block, key, comp, proj); \
            } \
        } \
        template <typename Proj> \
        [[nodiscard]] static std::size_t upper_bound(const TYPE* block, const TYPE& key, Comp comp, Proj proj) { \
            if constexpr (std::is_same_v<Proj, std::identity>) { \
                return detail::simd_ub_##TYPE##_##SUFFIX(block, key); \
            } else { \
                return scalar_block_searcher::upper_bound<TYPE, BLOCK_SIZE>(block, key, comp, proj); \
            } \
        } \
    };

DEFINE_SIMD_SPECIALIZATION(int64_t,  8, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(uint64_t, 8, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(int64_t,  8, IsStandardGreater, greater)
DEFINE_SIMD_SPECIALIZATION(uint64_t, 8, IsStandardGreater, greater)

DEFINE_SIMD_SPECIALIZATION(int32_t,  16, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(uint32_t, 16, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(int32_t,  16, IsStandardGreater, greater)
DEFINE_SIMD_SPECIALIZATION(uint32_t, 16, IsStandardGreater, greater)

DEFINE_SIMD_SPECIALIZATION(int16_t,  32, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(uint16_t, 32, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(int16_t,  32, IsStandardGreater, greater)
DEFINE_SIMD_SPECIALIZATION(uint16_t, 32, IsStandardGreater, greater)

DEFINE_SIMD_SPECIALIZATION(int8_t,   64, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(uint8_t,  64, IsStandardLess,    less)
DEFINE_SIMD_SPECIALIZATION(int8_t,   64, IsStandardGreater, greater)
DEFINE_SIMD_SPECIALIZATION(uint8_t,  64, IsStandardGreater, greater)

#undef DEFINE_SIMD_SPECIALIZATION
#endif

// --- Main Layout Policy ---

template <std::size_t B = 16>
struct implicit_btree_layout_policy {

    // --- Private Helper Implementations (Generic Logic) ---
private:
    template <typename T, typename Comp, typename Proj>
    static constexpr std::size_t lower_bound_generic(const T* base, std::size_t n, const T& value, Comp& comp, Proj& proj) {
        std::size_t k = 0;
        std::size_t result_idx = n;

        while (true) {
            std::size_t block_start = k * B;
            if (block_start >= n) break;

            std::size_t child_start = detail::btree_child_block_index(k, 0, B) * B;
            if (child_start < n) { LAYOUT_PREFETCH(&base[child_start]); }

            if (block_start + B <= n) {
                // Full Block: Delegate to block_searcher (which handles SIMD or Scalar)
                std::size_t idx_in_block = block_searcher<T, Comp, B>::lower_bound(
                    base + block_start, value, comp, proj
                );

                if (idx_in_block < B) result_idx = block_start + idx_in_block;
                k = detail::btree_child_block_index(k, idx_in_block, B);
            } else {
                // Tail Block: Always scalar
                std::size_t count = n - block_start;
                std::size_t idx_in_tail = scalar_block_searcher::lower_bound_n(
                    base + block_start, count, value, comp, proj
                );

                if (idx_in_tail < count) {
                    result_idx = block_start + idx_in_tail;
                    k = detail::btree_child_block_index(k, idx_in_tail, B);
                } else {
                    k = detail::btree_child_block_index(k, count, B);
                }
            }
        }
        return result_idx;
    }

    template <typename T, typename Comp, typename Proj>
    static constexpr std::size_t upper_bound_generic(const T* base, std::size_t n, const T& value, Comp& comp, Proj& proj) {
        std::size_t k = 0;
        std::size_t result_idx = n;

        while (true) {
            std::size_t block_start = k * B;
            if (block_start >= n) break;

            std::size_t child_start = detail::btree_child_block_index(k, 0, B) * B;
            if (child_start < n) { LAYOUT_PREFETCH(&base[child_start]); }

            if (block_start + B <= n) {
                std::size_t idx_in_block = block_searcher<T, Comp, B>::upper_bound(
                    base + block_start, value, comp, proj
                );

                if (idx_in_block < B) result_idx = block_start + idx_in_block;
                k = detail::btree_child_block_index(k, idx_in_block, B);
            } else {
                std::size_t count = n - block_start;
                std::size_t idx_in_tail = scalar_block_searcher::upper_bound_n(
                    base + block_start, count, value, comp, proj
                );

                if (idx_in_tail < count) {
                    result_idx = block_start + idx_in_tail;
                    k = detail::btree_child_block_index(k, idx_in_tail, B);
                } else {
                    k = detail::btree_child_block_index(k, count, B);
                }
            }
        }
        return result_idx;
    }

public:
    struct sorted_rank_to_index_fn {
        std::size_t operator()(std::size_t rank, std::size_t n) const { return detail::btree_sorted_rank_to_index(rank, n, B); }
    };
    struct index_to_sorted_rank_fn {
        std::size_t operator()(std::size_t index, std::size_t n) const { return detail::btree_index_to_sorted_rank(index, n, B); }
    };
    struct next_index_fn {
        std::ptrdiff_t operator()(std::ptrdiff_t i, std::size_t n_sz) const { return detail::btree_next_index(i, n_sz, B); }
    };
    struct prev_index_fn {
        std::ptrdiff_t operator()(std::ptrdiff_t i, std::size_t n_sz) const { return detail::btree_prev_index(i, n_sz, B); }
    };

    static constexpr inline sorted_rank_to_index_fn sorted_rank_to_index{};
    static constexpr inline index_to_sorted_rank_fn index_to_sorted_rank{};
    static constexpr inline next_index_fn next_index{};
    static constexpr inline prev_index_fn prev_index{};

    struct permute_fn {
        template<std::random_access_iterator I, std::sentinel_for<I> S>
        void operator()(I first, S last) const {
            const auto n = static_cast<std::size_t>(std::distance(first, last));
            if (n <= 1) return;
            using ValueT = std::iter_value_t<I>;
            std::vector<ValueT> temp;
            temp.resize(n);
            I current_source = first;
            fill_in_order(temp, current_source, 0, n);
            std::ranges::move(temp, first);
        }

        template<std::ranges::random_access_range R>
        void operator()(R&& range) const {
            (*this)(std::ranges::begin(range), std::ranges::end(range));
        }

    private:
        template<typename SrcIter, typename TempVec>
        static void fill_in_order(TempVec& temp, SrcIter& source_iter, std::size_t block_idx, std::size_t n) {
            std::size_t block_start = block_idx * B;
            if (block_start >= n) return;
            for (std::size_t i = 0; i <= B; ++i) {
                std::size_t child = detail::btree_child_block_index(block_idx, i, B);
                if (child * B < n) fill_in_order(temp, source_iter, child, n);
                if (i < B) {
                    std::size_t key_idx = block_start + i;
                    if (key_idx < n) { temp[key_idx] = *source_iter; ++source_iter; }
                }
            }
        }
    };
    static constexpr inline permute_fn permute{};

    struct get_nth_sorted_fn {
        template<std::ranges::random_access_range R>
        [[nodiscard]] constexpr std::ranges::range_reference_t<R> operator()(R&& range, std::size_t n) const {
            return *(std::ranges::begin(range) + sorted_rank_to_index(n, std::ranges::size(range)));
        }
    };
    static constexpr inline get_nth_sorted_fn get_nth_sorted{};

    struct lower_bound_fn {
        template<std::random_access_iterator I, std::sentinel_for<I> S,
                 typename T, typename Comp = std::ranges::less, typename Proj = std::identity>
        [[nodiscard]] constexpr I operator()(I first, S last, const T& value, Comp comp = {}, Proj proj = {}) const {
            if (first == last) return last;
            const auto n = static_cast<std::size_t>(std::distance(first, last));
            const auto* base = std::to_address(first);

#ifdef LAYOUT_USE_AVX2
            if constexpr (B == 8 && std::is_same_v<T, uint64_t> && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
                std::size_t idx = detail::search_loop_lb_uint64_less(base, n, value, B);
                return (idx == n) ? last : (first + idx);
            }
            else if constexpr (B == 8 && std::is_same_v<T, int64_t> && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
                std::size_t idx = detail::search_loop_lb_int64_less(base, n, value, B);
                return (idx == n) ? last : (first + idx);
            }
            else
#endif
            {
                std::size_t idx = lower_bound_generic(base, n, value, comp, proj);
                return (idx == n) ? last : (first + idx);
            }
        }

        template<std::ranges::random_access_range R,
                 typename T, typename Comp = std::ranges::less, typename Proj = std::identity>
        [[nodiscard]] constexpr std::ranges::iterator_t<R> operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {}) const {
            return (*this)(std::ranges::begin(range), std::ranges::end(range), value, std::ref(comp), std::ref(proj));
        }
    };
    static constexpr inline lower_bound_fn lower_bound{};

    struct upper_bound_fn {
        template<std::random_access_iterator I, std::sentinel_for<I> S,
                 typename T, typename Comp = std::ranges::less, typename Proj = std::identity>
        [[nodiscard]] constexpr I operator()(I first, S last, const T& value, Comp comp = {}, Proj proj = {}) const {
             if (first == last) return last;
            const auto n = static_cast<std::size_t>(std::distance(first, last));
            const auto* base = std::to_address(first);

#ifdef LAYOUT_USE_AVX2
            if constexpr (B == 8 && std::is_same_v<T, uint64_t> && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
                std::size_t idx = detail::search_loop_ub_uint64_less(base, n, value, B);
                return (idx == n) ? last : (first + idx);
            }
            else if constexpr (B == 8 && std::is_same_v<T, int64_t> && IsStandardLess<Comp, T> && std::is_same_v<Proj, std::identity>) {
                std::size_t idx = detail::search_loop_ub_int64_less(base, n, value, B);
                return (idx == n) ? last : (first + idx);
            }
            else
#endif
            {
                std::size_t idx = upper_bound_generic(base, n, value, comp, proj);
                return (idx == n) ? last : (first + idx);
            }
        }

        template<std::ranges::random_access_range R,
                 typename T, typename Comp = std::ranges::less, typename Proj = std::identity>
        [[nodiscard]] constexpr std::ranges::iterator_t<R> operator()(R&& range, const T& value, Comp comp = {}, Proj proj = {}) const {
            return (*this)(std::ranges::begin(range), std::ranges::end(range), value, std::ref(comp), std::ref(proj));
        }
    };
    static constexpr inline upper_bound_fn upper_bound{};
};

} // namespace eytzinger

#endif // IMPLICIT_BTREE_LAYOUT_POLICY_HPP
