#pragma once
#include <toggle/toggle.h>
using namespace parlay;
#include "hashbag.h"

static inline bool writeifnot(uint32_t& ref, uint32_t v){
    uint32_t old = ref;
    while (old != v && !__atomic_compare_exchange_n(&ref, &old, v, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return old != v;
}

template <class Graph>
toggle::Array<typename Graph::weight_type> BellmanFord(Graph& G, size_t source=0) {
    using T = typename Graph::weight_type;
    auto dist = toggle::Array<T>(G.n(), std::numeric_limits<T>::max());
    auto frontier = hashbag<uint32_t>(G.n());
    auto frt = toggle::Array<uint32_t>(G.n(), 0);
    auto in_next = toggle::Array<uint32_t>(G.n(), UINT32_MAX);
    dist[source] = 0; frontier.insert(source);

    for (uint32_t round = 0; round < G.n(); round++) {
        auto frt_size = frontier.pack_into(frt);
        if (!frt_size) break;
        toggle::parallel_for(0, frt_size, [&](uint32_t i) {
            uint32_t s = frt[i];
            T dist_s = dist[s];
            toggle::parallel_for(G.out_offset(s), G.out_offset(s + 1), [&](size_t i) {
                uint32_t d = G.out_edge(i);
                T nd = dist_s + G.weighted_out_edge(i).second;
                if (dist[d] > nd && toggle::write_min(dist[d], nd) && writeifnot(in_next[d], round)) frontier.insert(d);
            }, 256);
        }, 1);
    }
    return dist;
}