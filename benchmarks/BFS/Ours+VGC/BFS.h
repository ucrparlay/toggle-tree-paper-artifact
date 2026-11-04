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
    uint64_t mode = 0, stride = 1;

    for (uint64_t round = 0; ; round++) {
        if (mode == 0) {
            std::swap(frontier, next_frontier);
            if (frontier.empty()) break;
            if (reduce_edge() > (G.m() >> 3)) {
                frontier.consume_each([&](uint64_t s) { dist[s] = round; });
                mode = 1;
            }
            else if (round >= toggle::log2(G.n()) * 2) {
                frontier.consume_each([&](uint64_t s) { dist[s] = round; next_frontier.insert(s); });
                mode = 5; round--;
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
        else { // Local Search
            std::swap(frontier, next_frontier);
            if (frontier.empty()) break;
            if (mode % 5 == 0) {
                uint64_t new_stride = std::max<uint64_t>(1ULL, 1500000 * G.m() / (G.n() * frontier.reduce_size()));
                if (stride < new_stride) stride++;
                if (stride > new_stride) stride--;
            }
            auto local_search = [&](auto&& self, uint64_t v) -> void {
                if (G.out_offset(v+1) - G.out_offset(v) >= 1024) {
                    uint64_t s = v;
                    uint32_t min_dist_d = toggle::reduce<toggle::min, uint32_t(UINT32_MAX)>(G.in_offset(s), G.in_offset(s+1), [&](uint64_t i) {
                        uint64_t d = G.in_edge(i);
                        return dist[d];
                    });
                    uint64_t dist_s = min_dist_d + 1;
                    toggle::write_min(dist[s], dist_s);
                    toggle::parallel_for(G.out_offset(s), G.out_offset(s+1), [&](uint64_t i) {
                        uint64_t d = G.out_edge(i);
                        if (toggle::write_min(dist[d], dist_s + 1)) {
                            if (dist_s + 1 == round + stride) {  next_frontier.insert(d); }
                            else { self(self, d); }
                        }
                    }, 256);
                }
                else {
                    auto local_queue = toggle::LocalQueue(G, 1024, v);
                    while (true) {
                        uint64_t s = local_queue.pop_top();
                        if (s == UINT64_MAX) { break; }
                        uint32_t min_dist_d = UINT32_MAX;
                        for (uint64_t i = G.in_offset(s), ie = G.in_offset(s + 1); i < ie; i++) {
                            uint64_t d = G.in_edge(i);
                            uint32_t dist_d = toggle::atomic_read(dist[d]);
                            if (dist_d < min_dist_d) min_dist_d = dist_d;
                        }
                        uint64_t dist_s = min_dist_d + 1;
                        toggle::write_min(dist[s], dist_s);
                        for (uint64_t i = G.out_offset(s), ie = G.out_offset(s + 1); i < ie; i++) {
                            uint64_t d = G.out_edge(i);
                            if (toggle::write_min(dist[d], dist_s + 1)) {
                                if (dist_s + 1 == round + stride) { next_frontier.insert(d); }
                                else { local_queue.push_back(d); }
                            }
                        }
                    }
                    local_queue.for_each([&](uint64_t s) { self(self, s); });
                }
            };
            frontier.consume_each([&](uint64_t s) {
                if (toggle::atomic_read(dist[s]) != round) return;
                local_search(local_search, s);
            });
            round = round + stride - 1;
            mode++;
        }
    }

    return dist;
}

} // namespace toggle
