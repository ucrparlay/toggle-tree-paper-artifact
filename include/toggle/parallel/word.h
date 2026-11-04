// SPDX-License-Identifier: MIT
#pragma once
#include <bit>
#include <cstdint>
#include <type_traits>
#include "macros.h"

namespace toggle {

constexpr uint64_t pop_count(uint64_t word) noexcept {
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        return uint64_t(std::popcount(word));
    #else
        return uint64_t(__builtin_popcountll(word));
    #endif
}

constexpr uint64_t pop_offset(uint64_t i, uint64_t word) noexcept {
    #if defined(TOGGLE_TREE_WEAK_MEMORY) || !defined(__BMI2__)
        #if !defined(TOGGLE_TREE_WEAK_MEMORY)
            #warning "BMI2 is not enabled, toggle::IndexSet and toggle::IndexMap can not guarantee performance under this setting. Please compile with -mbmi2 or a suitable -march for benchmarking."
        #endif
        while (i--) { word &= word - 1; }
        return uint64_t(std::countr_zero(word));
    #else
        if (std::is_constant_evaluated()) {
            while (i--) { word &= word - 1; }
            return uint64_t(std::countr_zero(word));
        }
        return uint64_t(__builtin_ctzll(__builtin_ia32_pdep_di(1ULL << i, word)));
    #endif
}

constexpr uint64_t log2(uint64_t x) noexcept {
    return std::bit_width(x);
}

constexpr uint64_t glue32(uint64_t word_hi, uint64_t word_lo) noexcept {
    return (word_hi << 32) | word_lo;
}

constexpr uint64_t hi32(uint64_t word) noexcept {
    return word >> 32;
}

constexpr uint64_t lo32(uint64_t word) noexcept {
    return word & 0xFFFFFFFFULL;
}

#if !defined(TOGGLE_TREE_WEAK_MEMORY)
    #if defined(__AVX512F__)
        using vi32 = int __attribute__((vector_size(64), may_alias));
        using vi64 = long long __attribute__((vector_size(64), may_alias));
        using vf32 = float __attribute__((vector_size(64), may_alias));
        using vf64 = double __attribute__((vector_size(64), may_alias));
        template <class V>
        inline V load(const void* p) noexcept {
            static_assert(sizeof(V) == 64);
            V value;
            __builtin_memcpy(&value, p, sizeof(value));
            return value;
        }
        inline void stream_store(vi64* out, vi64 value) noexcept {
            #if defined(__clang__)
                __builtin_nontemporal_store(value, out);
            #else
                __builtin_ia32_movntdq512(out, value);
            #endif
        }
        inline void store_fence() noexcept {
            __builtin_ia32_sfence();
        }
    #endif
#endif

} // namespace toggle
