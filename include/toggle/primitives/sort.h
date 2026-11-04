// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>
#include "../parallel/macros.h"
#include "../parallel/parallel.h"
#include "../parallel/word.h"
#include "../data_structures/array.h"
#include "random.h"

namespace toggle {

template <class RandomIt, class T = typename std::iterator_traits<RandomIt>::value_type, class Compare = std::less<T>>
void parallel_sort(RandomIt start, RandomIt end, Compare comp = Compare{}) {
    uint64_t n = end - start;
    if (n < 8192) { std::sort(start, end, comp); return; }
    auto sqrt = [](uint64_t x) { return 1ULL << (std::bit_width(x - 1)) / 2; };
    uint64_t num_buckets = toggle::num_workers() * std::max<uint64_t>(1, sqrt(n*sizeof(T)/128)/toggle::num_workers());
    uint64_t block_size = (n + num_buckets - 1) / num_buckets;
    uint64_t num_blocks = (n + block_size - 1) / block_size;
    uint64_t num_samples = std::min<uint64_t>(n, num_buckets * 4);
    auto sample = toggle::Array<T>(num_samples, [&](uint64_t i) { return start[hash64(i) % n]; });
    std::sort(sample.begin(), sample.end(), comp);
    auto splitters = toggle::Array<T>(num_buckets - 1, [&](uint64_t i) { return sample[std::min<uint64_t>((i + 1) * num_samples / num_buckets, num_samples - 1)]; });
    auto buffer = toggle::Array<T>(n);
    auto counts = toggle::Array<uint64_t>(num_blocks * num_buckets, 0);
    toggle::parallel_for(0, num_blocks, [&](uint64_t block) {
        uint64_t local_start = block * block_size;
        uint64_t local_end = std::min<uint64_t>(n, local_start + block_size);
        std::sort(start + local_start, start + local_end, comp);
        uint64_t base = block * num_buckets;
        uint64_t bucket = 0;
        for (uint64_t i = local_start; i < local_end; i++) {
            T value = start[i]; buffer[i] = value;
            while (bucket < splitters.size() && comp(splitters[bucket], value)) { bucket++; }
            if (bucket < splitters.size() && !comp(value, splitters[bucket])) { counts[base + bucket + 1]++; }
            else { counts[base + bucket]++; }
        }
    }, 1);
    auto bucket_offsets = toggle::internal::histogram(num_buckets, num_blocks, counts);
    toggle::parallel_for(0, num_blocks, [&](uint64_t block) {
        uint64_t source = block * block_size;
        for (uint64_t bucket = 0; bucket < num_buckets; bucket++) {
            uint64_t destination = counts[block * num_buckets + bucket];
            uint64_t count = block + 1 < num_blocks ? counts[(block + 1) * num_buckets + bucket] - destination : bucket_offsets[bucket + 1] - destination;
            for (uint64_t i = 0; i < count; i++) { start[destination + i] = buffer[source + i]; }
            source += count;
        }
    }, 1);
    toggle::parallel_for(0, num_buckets, [&](uint64_t bucket) {
        bool all_equal = bucket > 0 && bucket < splitters.size() && !comp(splitters[bucket - 1], splitters[bucket]) && !comp(splitters[bucket], splitters[bucket - 1]);
        if (!all_equal) {
            std::sort(start + bucket_offsets[bucket], start + bucket_offsets[bucket + 1], comp);
        }
    }, 1);
}

template <class T, class Compare = std::less<T>>
void parallel_sort(toggle::Array<T>& A, Compare comp = Compare{}) {
    toggle::parallel_sort(A.begin(), A.begin() + A.size(), std::move(comp));
}

} // namespace toggle
