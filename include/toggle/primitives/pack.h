// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include "../parallel/parallel.h"
#include "../data_structures/array.h"

namespace toggle { 

template<class T, class Condition, class F>
toggle::Array<T> pack(uint64_t start, uint64_t end, Condition&& condition, F&& f) noexcept {
    uint64_t n = end - start;
    if (n < 8192) {
        uint64_t total = 0;
        for (uint64_t i = start; i < end; ++i) { total += bool(condition(i)); }
        auto output = toggle::Array<T>(total);
        uint64_t offset = 0;
        for (uint64_t i = start; i < end; ++i) { if (bool(condition(i))) output[offset++] = f(i); }
        return output;
    }
    uint64_t num_blocks = std::min<uint64_t>(toggle::num_workers() * 16, (n + 1023) / 1024);
    uint64_t block_size = (n + num_blocks - 1) / num_blocks;
    auto counts = toggle::Array<uint64_t>(num_blocks, [&](uint64_t block) {
        uint64_t local_start = start + block * block_size;
        uint64_t local_end = std::min(end, local_start + block_size);
        uint64_t count = 0;
        for (uint64_t i = local_start; i < local_end; ++i) { count += bool(condition(i)); }
        return count;
    }, 1);
    uint64_t total = 0;
    for (uint64_t block = 0; block < num_blocks; block++) { total += counts[block]; counts[block] = total - counts[block]; }
    auto output = toggle::Array<T>(total);
    toggle::parallel_for(0, num_blocks, [&](uint64_t block) {
        uint64_t local_start = start + block * block_size;
        uint64_t local_end = std::min(end, local_start + block_size);
        uint64_t offset = counts[block];
        for (uint64_t i = local_start; i < local_end; ++i) {
            if (bool(condition(i))) output[offset++] = f(i);
        }
    }, 1);
    return output;
}

template<class T, class Condition, class F>
uint64_t pack_into(toggle::Array<T>& output, uint64_t start, uint64_t end, Condition&& condition, F&& f) noexcept {
    uint64_t n = end - start;
    if (n < 8192) {
        uint64_t total = 0;
        for (uint64_t i = start; i < end; ++i) { total += bool(condition(i)); }
        uint64_t offset = 0;
        for (uint64_t i = start; i < end; ++i) { if (bool(condition(i))) output[offset++] = f(i); }
        return total;
    }
    uint64_t num_blocks = std::min<uint64_t>(toggle::num_workers() * 16, (n + 1023) / 1024);
    uint64_t block_size = (n + num_blocks - 1) / num_blocks;
    auto counts = toggle::Array<uint64_t>(num_blocks, [&](uint64_t block) {
        uint64_t local_start = start + block * block_size;
        uint64_t local_end = std::min(end, local_start + block_size);
        uint64_t count = 0;
        for (uint64_t i = local_start; i < local_end; ++i) { count += bool(condition(i)); }
        return count;
    }, 1);
    uint64_t total = 0;
    for (uint64_t block = 0; block < num_blocks; block++) { total += counts[block]; counts[block] = total - counts[block]; }
    toggle::parallel_for(0, num_blocks, [&](uint64_t block) {
        uint64_t local_start = start + block * block_size;
        uint64_t local_end = std::min(end, local_start + block_size);
        uint64_t offset = counts[block];
        for (uint64_t i = local_start; i < local_end; ++i) {
            if (bool(condition(i))) output[offset++] = f(i);
        }
    }, 1);
    return total;
}

} // namespace toggle
