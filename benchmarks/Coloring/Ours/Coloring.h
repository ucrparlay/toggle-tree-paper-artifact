// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <utility>
#include <toggle/toggle.h>

namespace toggle {

template <class Graph>
toggle::Array<uint32_t> Coloring(Graph& G) {
    auto color = toggle::Array<uint32_t>(G.n(), UINT32_MAX);
    auto frontier = toggle::IndexSet(G.n(), false);
    auto next_frontier = toggle::IndexSet(G.n(), false);
    auto num_blockers = toggle::Array<uint32_t>(G.n());
    auto used_color = toggle::Array<bool>(G.m(), false);

    toggle::parallel_for(0, G.n(), [&](uint64_t s){
        uint64_t deg_s = G.offset(s+1) - G.offset(s);
        if (deg_s == 0) { color[s] = 0; return; }
        num_blockers[s] = toggle::reduce<toggle::sum, uint32_t(0)>(G.offset(s), G.offset(s + 1), [&](uint64_t i) {
            uint64_t d = G.edge(i);
            uint64_t deg_d = G.offset(d + 1) - G.offset(d);
            return (deg_d > deg_s) || (deg_d == deg_s && toggle::hash32(d) < toggle::hash32(s));
        });
        if (num_blockers[s] == 0) { frontier.insert(s); }
    }, 1);

    while (!frontier.empty()) {
        frontier.consume_each([&](uint64_t s){
            uint64_t start = G.offset(s);
            uint64_t end = G.offset(s + 1);
            uint64_t deg = end - start;
            toggle::parallel_for(start, end, [&](uint64_t i) {
                uint64_t d = G.edge(i);
                uint32_t color_d = color[d];
                if (color_d == UINT32_MAX) {
                    if (toggle::fetch_and_sub(num_blockers[d], 1) == 1) {
                        next_frontier.insert(d);
                    }
                }
                else {
                    if (color_d < deg) { toggle::atomic_write(used_color[start + color_d], 1); }
                }
            }, 256);
            for (uint64_t i = start; i < end; i++) {
                if (used_color[i] == 0) { color[s] = i - start; return; }
            }
            color[s] = deg;
        });
        std::swap(frontier, next_frontier);
    }

    return color;
}

} // namespace toggle
