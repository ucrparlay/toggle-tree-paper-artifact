// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <toggle/toggle.h>

namespace toggle {

template <class Graph>
toggle::Array<uint32_t> BFS(Graph& G, uint64_t source = 0) {
    auto active = toggle::IndexSet(G.n(), true);
    auto frontier = toggle::IndexSet(G.n(), false);
    auto next_frontier = toggle::IndexSet(G.n(), false);
    auto reduce_edge = [&]() { return frontier.reduce<toggle::sum, uint64_t(0)>([&](uint64_t s) { return G.out_offset(s + 1) - G.out_offset(s); }); };
    auto dist = toggle::Array<uint32_t>(G.n(), UINT32_MAX);
    next_frontier.insert(source);
    active.remove(source);
    uint64_t mode = 0;

    for (uint64_t round = 0; ; round++) {
        if (mode == 0) {
            std::swap(frontier, next_frontier);
            if (frontier.empty()) break;
            if (reduce_edge() > (G.m() >> 3)) {
                frontier.consume_each([&](uint64_t s) { dist[s] = round; });
                mode = 1;
            }
            else {
                frontier.consume_each([&](uint64_t s) {
                    dist[s] = round;
                    toggle::parallel_for(G.out_offset(s), G.out_offset(s+1), [&](uint64_t i) {
                        uint64_t d = G.out_edge(i);
                        if (active.contains(d)) {
                            active.remove(d); next_frontier.insert(d);
                        }
                    }, 256);
                });
            }
        }
        else if (mode == 1) { // Direction Optimization
            active.for_each([&](uint64_t s) {
                for (uint64_t i = G.in_offset(s), ie = G.in_offset(s+1); i < ie; i++) {
                    uint64_t d = G.in_edge(i);
                    if (!active.contains(d)) {
                        next_frontier.insert(s);
                        return;
                    }
                }
            });
            std::swap(frontier, next_frontier);
            if (frontier.empty()) break;
            if (frontier.reduce_size() < (G.n() >> 6)) {
                frontier.consume_each([&](uint64_t s) { next_frontier.insert(s); active.remove(s); });
                round--; mode = 0;
            }
            else {
                frontier.consume_each([&](uint64_t s) { dist[s] = round; active.remove(s); });
            }
        }
    }

    return dist;
}

} // namespace toggle
