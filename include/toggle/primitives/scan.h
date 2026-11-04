// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <type_traits>
#include "../parallel/macros.h"
#include "../parallel/parallel.h"
#include "../parallel/word.h"
#include "../data_structures/array.h"

namespace toggle {

template<auto Combine, auto Identity, class F>
toggle::Array<decltype(Identity)> scan(uint64_t start, uint64_t end, F&& f) noexcept {
    using T = decltype(Identity);
    uint64_t n = end - start;
    auto output = toggle::Array<T>(n+1);
    if (n < 8192) {
        T total = Identity;
        for (uint64_t i = 0; i < n; i++) {
            output[i] = total;
            total = Combine(total, f(start + i));
        }
        output[n] = total;
        return output;
    }
    uint64_t block_size = 2048;
    uint64_t num_blocks = (n + block_size - 1) / block_size;
    auto offsets = toggle::Array<T>(num_blocks+1, [&](uint64_t block) {
        if (block == num_blocks) return T(Identity);
        uint64_t local_start = block * block_size;
        uint64_t local_end = std::min(n, local_start + block_size);
        T total = Identity;
        for (uint64_t i = local_start; i < local_end; i++) { total = Combine(total, f(start + i)); }
        return total;
    }, 1);
    T total = Identity;
    for (uint64_t i = 0; i <= num_blocks; i++) {
        T tmp = offsets[i];
        offsets[i] = total;
        total = Combine(total, tmp);
    }
    output[n] = total;
    toggle::parallel_for(0, num_blocks, [&](uint64_t block) {
        uint64_t local_start = block * block_size;
        uint64_t local_end = std::min(n, local_start + block_size);
        #if defined(TOGGLE_TREE_WEAK_MEMORY) || !defined(__AVX512F__)
            #if !defined(TOGGLE_TREE_WEAK_MEMORY)
                #warning "AVX512 is not enabled, toggle::scan can not guarantee performance under this setting. Please compile with -mavx512f or a suitable -march for benchmarking."
            #endif
            T offset = offsets[block];
            for (uint64_t i = local_start; i < local_end; i++) {
                output[i] = offset;
                offset = Combine(offset, f(start + i));
            }
        #else
            if constexpr (std::is_integral_v<T> && sizeof(T) <= 64 && 64 % sizeof(T) == 0) {
                constexpr uint64_t values_per_cache_line = 64 / sizeof(T);
                uint64_t streaming_end = local_start + (local_end - local_start) / values_per_cache_line * values_per_cache_line;
                T offset = offsets[block];
                for (uint64_t i = local_start; i < streaming_end; i += values_per_cache_line) {
                    alignas(64) T values[values_per_cache_line];
                    for (uint64_t j = 0; j < values_per_cache_line; ++j) {
                        values[j] = offset;
                        offset = Combine(offset, f(start + i + j));
                    }
                    toggle::stream_store(reinterpret_cast<toggle::vi64*>(&output[i]), toggle::load<toggle::vi64>(values));
                }
                for (uint64_t i = streaming_end; i < local_end; ++i) {
                    output[i] = offset;
                    offset = Combine(offset, f(start + i));
                }
                if (streaming_end != local_start) toggle::store_fence();
            }
            else {
                T offset = offsets[block];
                for (uint64_t i = local_start; i < local_end; i++) {
                    output[i] = offset;
                    offset = Combine(offset, f(start + i));
                }
            }
        #endif
    }, 1); 
    return output;
}

template<auto Combine, auto Identity, class Sequence>
toggle::Array<decltype(Identity)> scan(uint64_t start, uint64_t end, Sequence& input) noexcept {
    using T = decltype(Identity);
    return scan<Combine, Identity>(start, end, [&](uint64_t i) { return T(input[i]); });
}

namespace internal {
 
inline toggle::Array<uint64_t> histogram(uint64_t num_buckets, uint64_t num_blocks, toggle::Array<uint64_t>& counts) noexcept {
    auto bucket_offsets = toggle::Array<uint64_t>(num_buckets + 1);
    uint64_t num_groups = (num_buckets + 8 - 1) / 8;
    toggle::parallel_for(0, num_groups, [&](uint64_t g) {
        uint64_t b0 = g * 8, b1 = std::min(num_buckets, b0 + 8);
        uint64_t acc[8] = {0};
        for (uint64_t block = 0; block < num_blocks; ++block) {
            uint64_t base = block * num_buckets;
           for (uint64_t b = b0; b < b1; ++b) acc[b - b0] += counts[base + b];
        }
        for (uint64_t b = b0; b < b1; ++b) bucket_offsets[b] = acc[b - b0];
    }, 1);
    uint64_t total = 0;
    for (uint64_t bucket = 0; bucket < num_buckets; ++bucket) {
        total += bucket_offsets[bucket];
        bucket_offsets[bucket] = total - bucket_offsets[bucket];
    }
    bucket_offsets[num_buckets] = total;
    toggle::parallel_for(0, num_groups, [&](uint64_t g) {
        uint64_t b0 = g * 8, b1 = std::min(num_buckets, b0 + 8);
        uint64_t off[8];
        for (uint64_t b = b0; b < b1; ++b) off[b - b0] = bucket_offsets[b];
        for (uint64_t block = 0; block < num_blocks; ++block) {
            uint64_t base = block * num_buckets;
            for (uint64_t b = b0; b < b1; ++b) {
                uint64_t index = base + b;
                off[b - b0] += counts[index];
                counts[index] = off[b - b0] - counts[index];
            }
        }
    }, 1);
    return bucket_offsets;
}

} // namespace internal

} // namespace toggle
