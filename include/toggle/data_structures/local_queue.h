// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <tuple>
#include <type_traits>
#include "../parallel/parallel.h"
#include "array.h"
#include "index_set.h"
#include "../primitives/combine.h"

namespace toggle {

template <bool Direction, class Graph>
struct LocalQueueImpl {
    static constexpr uint64_t data_ahead = 2, edges_ahead = 6, data_cap = 16;

    Graph& G;
    uint64_t lpos, rpos, capacity;
    toggle::Array<uint32_t> local_queue;

    LocalQueueImpl(Graph& graph_, uint64_t capacity_, uint64_t s_)
        : G(graph_), lpos(0), rpos(1), capacity(capacity_ - 1), local_queue(capacity_) {
        local_queue[0] = uint32_t(s_);
    }

    uint64_t pop_top() noexcept {
        if (lpos == rpos) { return UINT64_MAX; }
        uint64_t s = local_queue[lpos];
        uint64_t workload = G.template out_offset<Direction>(s + 1) - G.template out_offset<Direction>(s);
        if (capacity < workload) { return UINT64_MAX; }
        lpos++; capacity -= workload;
        if (lpos + edges_ahead < rpos) {
            uint64_t p = local_queue[lpos + edges_ahead];
        }
        return s;
    }

    void push_back(uint64_t d) noexcept {
        local_queue[rpos++] = uint32_t(d);
    }

    template <class F> void for_each(F&& f) {
        uint32_t* data = local_queue.data_; uint64_t l = lpos, r = rpos;
        toggle::parallel_for(l, r, [&f, data](uint64_t i) { f(data[i]); }, 1);
    }

    template <class F> void sequential_for_each(F&& f) {
        for (uint64_t i = lpos; i < rpos; i++) { f(local_queue[i]); }
    }
};

template <bool Direction = true, class Graph>
inline auto LocalQueue(Graph& G, uint64_t capacity, uint64_t s) {
    return LocalQueueImpl<Direction, Graph>(G, capacity, s);
}

} // namespace toggle
