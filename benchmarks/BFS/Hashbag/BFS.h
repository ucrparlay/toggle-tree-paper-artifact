#pragma once
#include <cmath>
#include <toggle/toggle.h>
using namespace parlay;
#include "hashbag.h"

template <class Graph>
toggle::Array<uint32_t> BFS(Graph& G, size_t s = 0) {
    uint8_t mode = 0;
    auto active = toggle::IndexSet(G.n(), true);
    auto frontier = hashbag<uint32_t>(G.n());
    auto frontier_vertices = toggle::Array<uint32_t>(G.n());
    auto result = toggle::Array<uint32_t>(G.n(), UINT32_MAX);

    frontier.insert(s);
    active.remove(s);
    for (uint32_t round = 0;; round++) {
        if (mode == 0 || mode == 2) {
            size_t frontier_size = frontier.pack_into(frontier_vertices);
            if (!frontier_size) break;
            if (mode == 0 && round < uint32_t(2 * toggle::log2(G.n()))) {
                size_t frontier_edges = toggle::reduce<toggle::sum, uint64_t(0)>(0, frontier_size, [&](size_t i) {
                    uint32_t u = frontier_vertices[i];
                    return G.out_offset(u + 1) - G.out_offset(u);
                });
                if (frontier_edges > (G.m() >> 3)) {
                    toggle::parallel_for(0, frontier_size, [&](size_t i) {
                        result[frontier_vertices[i]] = round;
                    }, 1024);
                    mode = 1;
                    continue;
                }
            }
            toggle::parallel_for(0, frontier_size, [&](size_t i) {
                uint32_t u = frontier_vertices[i];
                result[u] = round;
                toggle::parallel_for(G.out_offset(u), G.out_offset(u + 1), [&](size_t j) {
                    uint32_t v = G.out_edge(j);
                    if (active.remove(v)) {
                        frontier.insert(v);
                    }
                }, 256);
            }, 1);
        } 
        else {
            active.for_each([&](size_t u) {
                for (size_t i = G.in_offset(u); i < G.in_offset(u + 1); i++) {
                    uint32_t v = G.in_edge(i);
                    if (!active.contains(v)) {
                        frontier.insert(u);
                        return;
                    }
                }
            });
            size_t frontier_size = frontier.pack_into(frontier_vertices);
            if (!frontier_size) break;
            if (frontier_size < (G.n() >> 6)) {
                toggle::parallel_for(0, frontier_size, [&](size_t i) {
                    uint32_t u = frontier_vertices[i];
                    frontier.insert(u);
                    active.remove(u);
                }, 1024);
                round--;
                mode = 2;
                continue;
            }
            toggle::parallel_for(0, frontier_size, [&](size_t i) {
                uint32_t u = frontier_vertices[i];
                result[u] = round;
                active.remove(u);
            }, 1024);
        }
    }
    return result;
}
