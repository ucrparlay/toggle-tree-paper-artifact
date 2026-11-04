// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <toggle/toggle.h>

namespace toggle {

template <class Graph>
toggle::Array<typename Graph::weight_type> wBFS(Graph& G, size_t source = 0) {
    using T = typename Graph::weight_type;
    auto dist = toggle::Array<T>(G.n(), std::numeric_limits<T>::max());
    auto tree = toggle::IndexMap(dist);
    dist[source] = 0; tree.insert(source);

    while (true) {
        T min_value = tree.repair();
        if (min_value == std::numeric_limits<T>::max()) break;
        T threshold = min_value;
        tree.extract_min(threshold, [&] (size_t s) {
            T dist_s = toggle::atomic_read(dist[s]);
            toggle::parallel_for(G.out_offset(s), G.out_offset(s + 1), [&](size_t i) {
                auto [d, w] = G.weighted_out_edge(i);
                if (toggle::write_min(dist[d], dist_s + w)) {
                    tree.insert(d);
                }
            }, 256);
        });
    }

    return dist;
}

}
