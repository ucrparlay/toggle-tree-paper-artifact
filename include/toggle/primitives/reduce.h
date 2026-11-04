// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "../parallel/parallel.h"

namespace toggle {

template <auto Combine, auto Identity, class F>
decltype(Identity) reduce(uint64_t start, uint64_t end, F&& f, uint64_t granularity = 1024) noexcept {
    using T = decltype(Identity);
    if (end - start <= granularity) {
        T aggregate = Identity;
        for (uint64_t i = start; i < end; i++) aggregate = Combine(aggregate, f(i));
        return aggregate;
    }
    uint64_t mid = start + (end - start) / 2;
    T left, right;
    toggle::parallel_do(
        [&]() { left  = reduce<Combine, Identity>(start, mid, f, granularity); },
        [&]() { right = reduce<Combine, Identity>(mid,   end, f, granularity); }
    );
    return Combine(left, right); 
}

} // namespace toggle
