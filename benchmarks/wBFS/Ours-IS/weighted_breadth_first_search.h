#pragma once
#include <toggle/toggle.h>

namespace toggle {

template <class Graph>
toggle::Array<typename Graph::weight_type> weighted_breadth_first_search_naive(Graph& G, size_t source=0) {
    using T = typename Graph::weight_type;
    auto dist = toggle::Array<T>(G.n(), std::numeric_limits<T>::max());
    auto active = toggle::IndexSet(G.n(), false);
    auto frontier = toggle::IndexSet(G.n(), false);
    auto next_frontier = toggle::IndexSet(G.n(), false);
    dist[source] = 0; active.insert(source);

    for (uint32_t round = 0; !active.empty(); round++) {
        active.for_each([&](uint32_t s) { 
            if (dist[s] == round) { active.remove(s); next_frontier.insert(s);}
        });
        std::swap(frontier, next_frontier);
        frontier.consume_each([&](uint32_t s) {
            T dist_s = dist[s];
            toggle::parallel_for(G.out_offset(s), G.out_offset(s + 1), [&](size_t i) {
                uint32_t d = G.out_edge(i);
                T w = G.weighted_out_edge(i).second;
                if (dist[d] > dist_s + w && toggle::write_min(dist[d], dist_s + w)) {
                    active.insert(d);
                }
            }, 256);
        });
    }
    return dist;
}

}
