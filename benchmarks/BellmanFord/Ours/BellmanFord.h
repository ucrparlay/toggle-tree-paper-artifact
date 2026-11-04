// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <toggle/toggle.h>

namespace toggle {

template <class Graph>
toggle::Array<typename Graph::weight_type> BellmanFord(Graph& G, uint64_t source = 0) {
    using T = typename Graph::weight_type;
    auto dist = toggle::Array<T>(G.n(), std::numeric_limits<T>::max());
    auto frontier = toggle::IndexSet(G.n(), false);
    auto next_frontier = toggle::IndexSet(G.n(), false);
    dist[source] = 0; frontier.insert(source);

    for (uint64_t round = 0; round < G.n() && !frontier.empty(); round++) {
        frontier.consume_each([&](uint64_t s) {
            T dist_s = toggle::atomic_read(dist[s]);
            toggle::parallel_for(G.out_offset(s), G.out_offset(s+1), [&](uint64_t i) {
                auto [d, w] = G.weighted_out_edge(i);
                if (toggle::write_min(dist[d], dist_s + w)) {
                    next_frontier.insert(d);
                }
            }, 256);
        });
        std::swap(frontier, next_frontier);
    }

    return dist;
}

}
