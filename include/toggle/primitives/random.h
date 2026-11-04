// Functions in this file produce exactly same output as parlaylib: github/cmuparlay/parlaylib
// Commit: 51017699dcc421f80479cdb238d3092233ad0d26

/*
MIT License

Copyright (c) 2020-present Guy Blelloch and other contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "../parallel/parallel.h"
#include "../data_structures/array.h"
#include "scan.h"

namespace toggle {

constexpr uint32_t hash32(uint32_t value) noexcept {
    value = (value + 0x7ed55d16U) + (value << 12);
    value = (value ^ 0xc761c23cU) ^ (value >> 19);
    value = (value + 0x165667b1U) + (value << 5);
    value = (value + 0xd3a2646cU) ^ (value << 9);
    value = (value + 0xfd7046c5U) + (value << 3);
    value = (value ^ 0xb55a4f09U) ^ (value >> 16);
    return value;
}

constexpr uint64_t hash64(uint64_t value) noexcept {
    value = value * 3935559000370003845ULL + 2691343689449507681ULL;
    value ^= value >> 21;
    value ^= value << 37;
    value ^= value >> 4;
    value *= 4768777513237032717ULL;
    value ^= value << 20;
    value ^= value >> 41;
    value ^= value << 5;
    return value;
}

constexpr uint32_t unhash32(uint32_t value) noexcept {
    value ^= 0xb55a4f09U;
    value ^= value >> 16;
    value = (value - 0xfd7046c5U) * 0x38e38e39U;
    const uint32_t y = value;
    uint32_t x = y - 0xd3a2646cU;
    x = (y ^ (x << 9)) - 0xd3a2646cU;
    x = (y ^ (x << 9)) - 0xd3a2646cU;
    x = (y ^ (x << 9)) - 0xd3a2646cU;
    value = (x - 0x165667b1U) * 0x3e0f83e1U;
    value ^= 0xc761c23cU;
    value ^= value >> 19;
    return (value - 0x7ed55d16U) * 0x00fff001U;
}

constexpr uint64_t unhash64(uint64_t value) noexcept {
    value ^= value << 5;
    value ^= value << 10;
    value ^= value << 20;
    value ^= value << 40;
    value ^= value >> 41;
    value ^= value << 20;
    value ^= value << 40;
    value *= 0x166a74154b2b37c5ULL;
    value ^= value >> 4;
    value ^= value >> 8;
    value ^= value >> 16;
    value ^= value >> 32;
    value ^= value << 37;
    value ^= value >> 21;
    value ^= value >> 42;
    return (value - 2691343689449507681ULL) * 0xbe21f44c6018e14dULL;
}

template <class T>
toggle::Array<T> random_permutation(uint64_t n, uint64_t seed = 0) noexcept {
    static_assert(std::is_integral_v<T>);
    auto seq_random_shuffle = [](toggle::Array<T>& values, uint64_t first, uint64_t last, uint64_t state = 0) noexcept {
        if (last - first < 2) return;
        for (uint64_t i = last - first - 1; i != 0; --i) {
            std::swap(values[first + i], values[first + hash64(i + state) % (i + 1)]);
        }
    };
    if (n < 8192) {
        auto input = toggle::Array<T>(n, [](T i) { return i; });
        seq_random_shuffle(input, 0, n, seed);
        return input;
    }
    uint64_t num_buckets = 1ULL << ((n < (1ULL << 27)) ? (std::bit_width(n - 1) - 7) / 2 : std::bit_width(n - 1) - 17);
    uint64_t num_blocks = toggle::num_workers() * 2;
    uint64_t block_size = (n + num_blocks - 1) / num_blocks;
    auto counts = toggle::Array<uint64_t>(num_blocks * num_buckets, 0);
    toggle::parallel_for(0, num_blocks, [&](uint64_t block) {
        uint64_t first = block * block_size;
        uint64_t last = std::min(first + block_size, n);
        uint64_t base = block * num_buckets;
        for (uint64_t i = first; i < last; i++) counts[base + (hash64(i + seed) & (num_buckets - 1))]++;
    }, 1);
    auto bucket_offsets = toggle::internal::histogram(num_buckets, num_blocks, counts);
    auto output = toggle::Array<T>(n);
    toggle::parallel_for(0, num_blocks, [&](uint64_t block) {
        uint64_t first = block * block_size;
        uint64_t last = (std::min)(first + block_size, n);
        uint64_t base = block * num_buckets;
        for (uint64_t i = first; i < last; ++i) {
            output[counts[base + (hash64(i + seed) & (num_buckets - 1))]++] = i;
        }
    }, 1);
    toggle::parallel_for(0, num_buckets, [&](uint64_t bucket) {
        seq_random_shuffle(output, bucket_offsets[bucket], bucket_offsets[bucket + 1], toggle::hash64(toggle::hash64(bucket + seed)));
    }, 1);
    return output;
}

} // namespace toggle
