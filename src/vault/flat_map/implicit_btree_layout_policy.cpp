#include <vault/flat_map/implicit_btree_layout_policy.hpp>

#ifdef LAYOUT_USE_AVX2
#include <immintrin.h>
#endif

namespace eytzinger::detail {

// -----------------------------------------------------------------------------
// Core Arithmetic Helpers (Internal)
// -----------------------------------------------------------------------------

static std::size_t child_block_index(std::size_t block_idx, std::size_t child_slot, std::size_t B) {
    return block_idx * (B + 1) + 1 + child_slot;
}

static std::size_t parent_block_index(std::size_t block_idx, std::size_t B) {
    return (block_idx == 0) ? 0 : (block_idx - 1) / (B + 1);
}

static std::size_t which_child(std::size_t block_idx, std::size_t B) {
    return (block_idx == 0) ? 0 : (block_idx - 1) % (B + 1);
}

static std::size_t subtree_size(std::size_t block_idx, std::size_t n, std::size_t B) {
    std::size_t start_idx = block_idx * B;
    if (start_idx >= n) return 0;

    std::size_t size = 0;
    std::size_t current_level_blocks = 1;
    std::size_t first_block = block_idx;

    while (true) {
        std::size_t level_start_idx = first_block * B;
        if (level_start_idx >= n) break;

        std::size_t level_capacity = current_level_blocks * B;
        std::size_t level_end_idx = level_start_idx + level_capacity;

        if (n >= level_end_idx) {
            size += level_capacity;
        } else {
            size += (n - level_start_idx);
            break;
        }

        first_block = child_block_index(first_block, 0, B);
        current_level_blocks *= (B + 1);
    }
    return size;
}

static std::ptrdiff_t find_max_in_subtree(std::size_t start_block, std::size_t n_sz, std::size_t B) {
    std::size_t curr = start_block;
    while (true) {
        bool moved = false;
        std::size_t c_b = child_block_index(curr, B, B);
        if (c_b * B < n_sz) {
            curr = c_b;
            continue;
        }
        for (int k = static_cast<int>(B) - 1; k >= 0; --k) {
            std::size_t key_idx = curr * B + k;
            if (key_idx < n_sz) return static_cast<std::ptrdiff_t>(key_idx);

            std::size_t c_k = child_block_index(curr, k, B);
            if (c_k * B < n_sz) {
                curr = c_k;
                moved = true;
                break;
            }
        }
        if (!moved) return -1;
    }
}

// -----------------------------------------------------------------------------
// Exposed Topology/Traversal Functions
// -----------------------------------------------------------------------------

std::size_t btree_child_block_index(std::size_t block_idx, std::size_t child_slot, std::size_t B) {
    return child_block_index(block_idx, child_slot, B);
}

std::size_t btree_sorted_rank_to_index(std::size_t rank, std::size_t n, std::size_t B) {
    std::size_t current_block = 0;
    while (true) {
        if (current_block * B >= n) return n;
        for (std::size_t i = 0; i < B; ++i) {
            std::size_t c_idx = child_block_index(current_block, i, B);
            std::size_t left_size = subtree_size(c_idx, n, B);
            if (rank < left_size) {
                current_block = c_idx;
                goto continue_descent;
            }
            rank -= left_size;
            std::size_t key_idx = current_block * B + i;
            if (key_idx >= n) return n;
            if (rank == 0) return key_idx;
            rank--;
        }
        current_block = child_block_index(current_block, B, B);
        continue_descent:;
    }
}

std::size_t btree_index_to_sorted_rank(std::size_t index, std::size_t n, std::size_t B) {
    if (index >= n) return n;
    std::size_t rank = 0;
    std::size_t current_block = index / B;
    std::size_t slot = index % B;
    for (std::size_t i = 0; i <= slot; ++i) {
        rank += subtree_size(child_block_index(current_block, i, B), n, B);
        if (i < slot) rank++;
    }
    while (current_block > 0) {
        std::size_t parent = parent_block_index(current_block, B);
        std::size_t child_id = which_child(current_block, B);
        for (std::size_t i = 0; i < child_id; ++i) {
            rank += subtree_size(child_block_index(parent, i, B), n, B);
            if (i < B) rank++;
        }
        current_block = parent;
    }
    return rank;
}

std::ptrdiff_t btree_next_index(std::ptrdiff_t i, std::size_t n_sz, std::size_t B) {
    if (i < 0 || static_cast<std::size_t>(i) >= n_sz) return -1;
    std::size_t curr = static_cast<std::size_t>(i);
    std::size_t block = curr / B;
    std::size_t slot  = curr % B;
    std::size_t right_child = child_block_index(block, slot + 1, B);
    if (right_child * B < n_sz) {
        curr = right_child;
        while (true) {
            std::size_t left_child = child_block_index(curr, 0, B);
            if (left_child * B >= n_sz) break;
            curr = left_child;
        }
        return static_cast<std::ptrdiff_t>(curr * B);
    }
    if (slot < B - 1) {
        std::size_t next_slot = block * B + slot + 1;
        if (next_slot < n_sz) return static_cast<std::ptrdiff_t>(next_slot);
    }
    while (block > 0) {
        std::size_t p = parent_block_index(block, B);
        std::size_t c_idx = which_child(block, B);
        if (c_idx < B) return static_cast<std::ptrdiff_t>(p * B + c_idx);
        block = p;
    }
    return -1;
}

std::ptrdiff_t btree_prev_index(std::ptrdiff_t i, std::size_t n_sz, std::size_t B) {
    if (i == -1) {
        if (n_sz == 0) return -1;
        return find_max_in_subtree(0, n_sz, B);
    }
    std::size_t curr = static_cast<std::size_t>(i);
    std::size_t block = curr / B;
    std::size_t slot  = curr % B;
    std::size_t left_child = child_block_index(block, slot, B);
    if (left_child * B < n_sz) {
        return find_max_in_subtree(left_child, n_sz, B);
    }
    if (slot > 0) return static_cast<std::ptrdiff_t>(block * B + slot - 1);
    while (block > 0) {
        std::size_t p = parent_block_index(block, B);
        std::size_t c_idx = which_child(block, B);
        if (c_idx > 0) return static_cast<std::ptrdiff_t>(p * B + c_idx - 1);
        block = p;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// SIMD IMPLEMENTATIONS
// -----------------------------------------------------------------------------

#ifdef LAYOUT_USE_AVX2

// --- Helper: XOR Sign Bit ---
template <typename T>
__m256i xor_sign_bit(__m256i v) {
    if constexpr (sizeof(T) == 8) {
        return _mm256_xor_si256(v, _mm256_set1_epi64x(0x8000000000000000ULL));
    } else if constexpr (sizeof(T) == 4) {
        return _mm256_xor_si256(v, _mm256_set1_epi32(0x80000000));
    } else if constexpr (sizeof(T) == 2) {
        return _mm256_xor_si256(v, _mm256_set1_epi16(static_cast<short>(0x8000)));
    } else if constexpr (sizeof(T) == 1) {
        return _mm256_xor_si256(v, _mm256_set1_epi8(static_cast<char>(0x80)));
    }
}

// --- Macros for SIMD Comparisons ---

// 64-bit (int64/uint64)
#define IMPL_LB_64(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi64x(static_cast<long long>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 4))); \
    int m = _mm256_movemask_pd(_mm256_castsi256_pd(CMP_INTRIN(kv, v0))) | \
           (_mm256_movemask_pd(_mm256_castsi256_pd(CMP_INTRIN(kv, v1))) << 4); \
    return static_cast<std::size_t>(std::popcount(static_cast<uint32_t>(m))); \
}

#define IMPL_UB_64(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi64x(static_cast<long long>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 4))); \
    int m = _mm256_movemask_pd(_mm256_castsi256_pd(CMP_INTRIN(v0, kv))) | \
           (_mm256_movemask_pd(_mm256_castsi256_pd(CMP_INTRIN(v1, kv))) << 4); \
    return static_cast<std::size_t>(std::countr_zero(static_cast<uint32_t>(m | (1 << 8)))); \
}

// 32-bit (int32/uint32)
#define IMPL_LB_32(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi32(static_cast<int>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 8))); \
    int m = _mm256_movemask_ps(_mm256_castsi256_ps(CMP_INTRIN(kv, v0))) | \
           (_mm256_movemask_ps(_mm256_castsi256_ps(CMP_INTRIN(kv, v1))) << 8); \
    return static_cast<std::size_t>(std::popcount(static_cast<uint32_t>(m))); \
}

#define IMPL_UB_32(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi32(static_cast<int>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 8))); \
    int m = _mm256_movemask_ps(_mm256_castsi256_ps(CMP_INTRIN(v0, kv))) | \
           (_mm256_movemask_ps(_mm256_castsi256_ps(CMP_INTRIN(v1, kv))) << 8); \
    return static_cast<std::size_t>(std::countr_zero(static_cast<uint32_t>(m | (1 << 16)))); \
}

// 16-bit (int16/uint16)
#define IMPL_LB_16(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi16(static_cast<short>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 16))); \
    uint32_t m0 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(kv, v0))); \
    uint32_t m1 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(kv, v1))); \
    return static_cast<std::size_t>((std::popcount(m0) + std::popcount(m1)) / 2); \
}

#define IMPL_UB_16(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi16(static_cast<short>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 16))); \
    uint32_t m0 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(v0, kv))); \
    uint32_t m1 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(v1, kv))); \
    uint64_t fm = static_cast<uint64_t>(m0) | (static_cast<uint64_t>(m1) << 32); \
    if (fm == 0) return 32; \
    return static_cast<std::size_t>(std::countr_zero(fm) / 2); \
}

// 8-bit (int8/uint8)
#define IMPL_LB_8(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi8(static_cast<char>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 32))); \
    uint32_t m0 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(kv, v0))); \
    uint32_t m1 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(kv, v1))); \
    return static_cast<std::size_t>(std::popcount(m0) + std::popcount(m1)); \
}

#define IMPL_UB_8(NAME, T, CAST, CMP_INTRIN, PRE_OP) \
std::size_t NAME(const T* b, T k) { \
    __m256i kv = PRE_OP(T, _mm256_set1_epi8(static_cast<char>(k))); \
    __m256i v0 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b))); \
    __m256i v1 = PRE_OP(T, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 32))); \
    uint32_t m0 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(v0, kv))); \
    uint32_t m1 = static_cast<uint32_t>(_mm256_movemask_epi8(CMP_INTRIN(v1, kv))); \
    uint64_t fm = static_cast<uint64_t>(m0) | (static_cast<uint64_t>(m1) << 32); \
    if (fm == 0) return 64; \
    return static_cast<std::size_t>(std::countr_zero(fm)); \
}

#define NO_OP(T, x) x
#define XOR_SIGN(T, x) xor_sign_bit<T>(x)

// --- Instantiations (32 total) ---

// int64 (B=8)
IMPL_LB_64(simd_lb_int64_less,    int64_t, long long, _mm256_cmpgt_epi64, NO_OP)
IMPL_UB_64(simd_ub_int64_less,    int64_t, long long, _mm256_cmpgt_epi64, NO_OP)
IMPL_LB_64(simd_lb_int64_greater, int64_t, long long, _mm256_cmpgt_epi64, NO_OP) // Note: Logic same, just inverted call in UB/LB macro use
IMPL_UB_64(simd_ub_int64_greater, int64_t, long long, _mm256_cmpgt_epi64, NO_OP)

// uint64 (B=8)
IMPL_LB_64(simd_lb_uint64_less,    uint64_t, long long, _mm256_cmpgt_epi64, XOR_SIGN)
IMPL_UB_64(simd_ub_uint64_less,    uint64_t, long long, _mm256_cmpgt_epi64, XOR_SIGN)
IMPL_LB_64(simd_lb_uint64_greater, uint64_t, long long, _mm256_cmpgt_epi64, XOR_SIGN)
IMPL_UB_64(simd_ub_uint64_greater, uint64_t, long long, _mm256_cmpgt_epi64, XOR_SIGN)

// int32 (B=16)
IMPL_LB_32(simd_lb_int32_less,    int32_t, int, _mm256_cmpgt_epi32, NO_OP)
IMPL_UB_32(simd_ub_int32_less,    int32_t, int, _mm256_cmpgt_epi32, NO_OP)
IMPL_LB_32(simd_lb_int32_greater, int32_t, int, _mm256_cmpgt_epi32, NO_OP)
IMPL_UB_32(simd_ub_int32_greater, int32_t, int, _mm256_cmpgt_epi32, NO_OP)

// uint32 (B=16)
IMPL_LB_32(simd_lb_uint32_less,    uint32_t, int, _mm256_cmpgt_epi32, XOR_SIGN)
IMPL_UB_32(simd_ub_uint32_less,    uint32_t, int, _mm256_cmpgt_epi32, XOR_SIGN)
IMPL_LB_32(simd_lb_uint32_greater, uint32_t, int, _mm256_cmpgt_epi32, XOR_SIGN)
IMPL_UB_32(simd_ub_uint32_greater, uint32_t, int, _mm256_cmpgt_epi32, XOR_SIGN)

// int16 (B=32)
IMPL_LB_16(simd_lb_int16_less,    int16_t, short, _mm256_cmpgt_epi16, NO_OP)
IMPL_UB_16(simd_ub_int16_less,    int16_t, short, _mm256_cmpgt_epi16, NO_OP)
IMPL_LB_16(simd_lb_int16_greater, int16_t, short, _mm256_cmpgt_epi16, NO_OP)
IMPL_UB_16(simd_ub_int16_greater, int16_t, short, _mm256_cmpgt_epi16, NO_OP)

// uint16 (B=32)
IMPL_LB_16(simd_lb_uint16_less,    uint16_t, short, _mm256_cmpgt_epi16, XOR_SIGN)
IMPL_UB_16(simd_ub_uint16_less,    uint16_t, short, _mm256_cmpgt_epi16, XOR_SIGN)
IMPL_LB_16(simd_lb_uint16_greater, uint16_t, short, _mm256_cmpgt_epi16, XOR_SIGN)
IMPL_UB_16(simd_ub_uint16_greater, uint16_t, short, _mm256_cmpgt_epi16, XOR_SIGN)

// int8 (B=64)
IMPL_LB_8(simd_lb_int8_less,    int8_t, char, _mm256_cmpgt_epi8, NO_OP)
IMPL_UB_8(simd_ub_int8_less,    int8_t, char, _mm256_cmpgt_epi8, NO_OP)
IMPL_LB_8(simd_lb_int8_greater, int8_t, char, _mm256_cmpgt_epi8, NO_OP)
IMPL_UB_8(simd_ub_int8_greater, int8_t, char, _mm256_cmpgt_epi8, NO_OP)

// uint8 (B=64)
IMPL_LB_8(simd_lb_uint8_less,    uint8_t, char, _mm256_cmpgt_epi8, XOR_SIGN)
IMPL_UB_8(simd_ub_uint8_less,    uint8_t, char, _mm256_cmpgt_epi8, XOR_SIGN)
IMPL_LB_8(simd_lb_uint8_greater, uint8_t, char, _mm256_cmpgt_epi8, XOR_SIGN)
IMPL_UB_8(simd_ub_uint8_greater, uint8_t, char, _mm256_cmpgt_epi8, XOR_SIGN)


// -----------------------------------------------------------------------------
// Fast Path Loops (64-bit Less)
// -----------------------------------------------------------------------------

#define IMPL_FAST_PATH_LOOPS(SUFFIX, TYPE, SIMD_LB, SIMD_UB) \
std::size_t search_loop_lb_##SUFFIX(const TYPE* base, std::size_t n, TYPE key, std::size_t B) { \
    std::size_t k = 0; \
    std::size_t result_idx = n; \
    while (true) { \
        std::size_t block_start = k * B; \
        if (block_start >= n) break; \
        std::size_t child_start = child_block_index(k, 0, B) * B; \
        if (child_start < n) { _mm_prefetch(reinterpret_cast<const char*>(base + child_start), _MM_HINT_T0); } \
        if (block_start + B <= n) { \
            std::size_t idx_in_block = SIMD_LB(base + block_start, key); \
            if (idx_in_block < B) result_idx = block_start + idx_in_block; \
            k = child_block_index(k, idx_in_block, B); \
        } else { \
            std::size_t i = block_start; \
            for (; i < n; ++i) { \
                if (!(base[i] < key)) { /* !comp(elem, key) */ \
                    result_idx = i; \
                    k = child_block_index(k, i - block_start, B); \
                    goto next_level; \
                } \
            } \
            k = child_block_index(k, n - block_start, B); \
            next_level:; \
        } \
    } \
    return result_idx; \
} \
std::size_t search_loop_ub_##SUFFIX(const TYPE* base, std::size_t n, TYPE key, std::size_t B) { \
    std::size_t k = 0; \
    std::size_t result_idx = n; \
    while (true) { \
        std::size_t block_start = k * B; \
        if (block_start >= n) break; \
        std::size_t child_start = child_block_index(k, 0, B) * B; \
        if (child_start < n) { _mm_prefetch(reinterpret_cast<const char*>(base + child_start), _MM_HINT_T0); } \
        if (block_start + B <= n) { \
            std::size_t idx_in_block = SIMD_UB(base + block_start, key); \
            if (idx_in_block < B) result_idx = block_start + idx_in_block; \
            k = child_block_index(k, idx_in_block, B); \
        } else { \
            std::size_t i = block_start; \
            for (; i < n; ++i) { \
                if (key < base[i]) { \
                    result_idx = i; \
                    k = child_block_index(k, i - block_start, B); \
                    goto next_level; \
                } \
            } \
            k = child_block_index(k, n - block_start, B); \
            next_level:; \
        } \
    } \
    return result_idx; \
}

IMPL_FAST_PATH_LOOPS(uint64_less, uint64_t, simd_lb_uint64_less, simd_ub_uint64_less)
IMPL_FAST_PATH_LOOPS(int64_less,  int64_t,  simd_lb_int64_less,  simd_ub_int64_less)

#undef IMPL_FAST_PATH_LOOPS
#undef IMPL_LB_64
#undef IMPL_UB_64
#undef IMPL_LB_32
#undef IMPL_UB_32
#undef IMPL_LB_16
#undef IMPL_UB_16
#undef IMPL_LB_8
#undef IMPL_UB_8
#undef NO_OP
#undef XOR_SIGN

#endif // LAYOUT_USE_AVX2

} // namespace eytzinger::detail
