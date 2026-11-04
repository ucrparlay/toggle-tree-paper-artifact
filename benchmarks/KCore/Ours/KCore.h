#pragma once
#include <toggle/toggle.h>

namespace toggle {

template <class Graph>
toggle::Array<uint32_t> KCore(Graph& G) {
    auto coreness = toggle::Array<uint32_t>(G.n(), 0);
    auto active = toggle::IndexSet(G.n(), true);
    auto frontier = toggle::IndexSet(G.n(), false);
    auto next_frontier = toggle::IndexSet(G.n(), false);
    auto degree = toggle::Array<uint32_t>(G.n(), [&](size_t s){
        if (G.offset(s+1) - G.offset(s) == 0) { active.remove(s); }
        return G.offset(s+1) - G.offset(s);
    });

    uint32_t k = 0; int32_t use_reduce = -2*toggle::log2(G.n());
    while (!active.empty()) {
        k = use_reduce>=0 ? active.reduce<toggle::min, uint32_t(UINT32_MAX)>([&](uint64_t i) { return degree[i]; }): k+1;
        active.for_each([&](uint32_t s) {
            if (degree[s] == k) { active.remove(s); frontier.insert(s); }
        });
        if (use_reduce<0 && frontier.empty()) { use_reduce++; }
        while (!frontier.empty()) {
            frontier.consume_each([&](uint32_t s) {
                coreness[s] = k;
                toggle::parallel_for(G.offset(s), G.offset(s + 1), [&](size_t j) {
                    uint32_t d = G.edge(j);
                    if (active.contains(d)) {
                        if (toggle::fetch_and_sub(degree[d], 1) == k+1) {
                            active.remove(d); next_frontier.insert(d);
                        }
                    }
                }, 256);
            });
            std::swap(frontier, next_frontier);
        }
    }

    return coreness;
}

} // namespace toggle
