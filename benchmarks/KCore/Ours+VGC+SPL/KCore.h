// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <toggle/toggle.h>

namespace toggle {

template <class Graph>
toggle::Array<uint32_t> KCore(Graph& G) {
    struct Sampler {
        uint32_t sample_count;
        uint32_t sample_rate;

        bool sample() noexcept {
            static thread_local uint32_t dice = toggle::worker_id() + 1;
            dice ^= dice << 13; dice ^= dice >> 7; dice ^= dice << 17;
            if (dice >= sample_rate) { return false; }
            return toggle::fetch_and_sub(sample_count, 1) == 1;
        }

        bool sample(uint32_t value) noexcept {
            static thread_local uint32_t dice = toggle::worker_id() + 1;
            for (uint32_t i = 0; i < value; i++) {
                dice ^= dice << 13; dice ^= dice >> 7; dice ^= dice << 17;
                if (dice < sample_rate) { if (sample_count-- == 1) { return true; }}
            }
            return false;
        }

        void reset(uint32_t total) noexcept {
            sample_count = toggle::log2(total) * toggle::log2(total) * toggle::log2(total);
            sample_rate = std::min(1.0, double(sample_count) / double(0.7 * total)) * UINT32_MAX;
        }
    };

    auto degree = toggle::Array<uint32_t>(G.n());
    auto frontier = toggle::IndexSet(G.n(), false);
    auto next_frontier = toggle::IndexSet(G.n(), false);
    auto active = toggle::IndexSet(G.n(), true);
    auto resample = toggle::IndexSet(G.n(), false);
    auto sample_mode = toggle::IndexSet(G.n(), false);
    auto samplers = toggle::Array<Sampler>(G.n());
    uint32_t k = 0;
    auto peel = [&](uint32_t d) -> bool {
        uint32_t old = toggle::fetch_and_sub(degree[d], 1);
        if (old <= k) [[unlikely]] { toggle::fetch_and_add(degree[d], 1); }
        return old == k + 1;
    };
    uint32_t no_reduce = 2 * toggle::log2(G.n());
    uint32_t sample_threshold = 16384;

    toggle::parallel_for(0, G.n(), [&](size_t i) {
        degree[i] = G.offset(i + 1) - G.offset(i);
        if (degree[i] == 0) { active.remove(i); }
        if (degree[i] >= sample_threshold) {
            sample_mode.insert(i);
            samplers[i].reset(degree[i]);
        }
    }, 1024);

    while (!active.empty()) {
        uint32_t delta = no_reduce == 0 ? active.reduce<toggle::min, uint32_t(UINT32_MAX)>([&](size_t i) { return degree[i]; }) - k : 1;
        k += delta;
        sample_threshold = std::max<uint32_t>(sample_threshold, 10 * k);

        sample_mode.for_each([&](uint32_t s) {
            if (samplers[s].sample(delta)) {
                degree[s] = toggle::reduce<toggle::sum, uint32_t(0)>(G.offset(s), G.offset(s + 1), [&](size_t i) {
                    return active.contains(G.edge(i)) || frontier.contains(G.edge(i));
                });
                if (degree[s] < k) { degree[s] = k; active.remove(s); sample_mode.remove(s); frontier.insert(s); }
                if (degree[s] - k >= sample_threshold) { samplers[s].reset(degree[s] - k); }
                else { sample_mode.remove(s);}
            }
        });

        active.for_each([&](uint32_t s) {
            if (degree[s] == k) {
                frontier.insert(s);
                active.remove(s);
            }
        });

        if (no_reduce > 0 && frontier.empty()) { no_reduce--; }

        while (!frontier.empty()) {
            auto local_search = [&](auto&& self, uint32_t v, uint32_t layer) -> void {
                if (G.offset(v+1) - G.offset(v) >= 128) {
                    uint32_t s = v;
                    toggle::parallel_for(G.offset(s), G.offset(s+1), [&](size_t i) {
                        uint32_t d = G.edge(i);
                        if (active.contains(d)) {
                            if (sample_mode.contains(d)) {
                                if (samplers[d].sample()) { resample.insert(d); }
                            }
                            else {
                                if (peel(d)) {
                                    active.remove(d);
                                    if (layer == 0) next_frontier.insert(d);
                                    else self(self, d, layer - 1);
                                }
                            }
                        }
                    }, 128);
                }
                else {
                    auto local_queue = toggle::LocalQueue(G, 128, v);
                    while (true) {
                        uint64_t s = local_queue.pop_top();
                        if (s == UINT64_MAX) { break; }
                        for (uint64_t i = G.offset(s), ie = G.offset(s+1); i < ie; i++) {
                            uint64_t d = G.edge(i);
                            if (active.contains(d)) {
                                if (!sample_mode.empty() && sample_mode.contains(d)) {
                                    if (samplers[d].sample()) { resample.insert(d); }
                                }
                                else {
                                    if (peel(d)) {
                                        active.remove(d);
                                        local_queue.push_back(d);
                                    }
                                }
                            }
                        }
                    }
                    if (layer == 0) { local_queue.sequential_for_each([&](uint64_t s) { next_frontier.insert(s); }); }
                    else { local_queue.for_each([&](uint64_t s) { self(self, s, layer - 1); }); }
                }
            };
            frontier.consume_each([&](uint32_t s) {
                local_search(local_search, s, 8);
            });
            resample.consume_each([&](uint32_t s) {
                degree[s] = toggle::reduce<toggle::sum, uint32_t(0)>(G.offset(s), G.offset(s + 1), [&](size_t i) {
                    return active.contains(G.edge(i)) || next_frontier.contains(G.edge(i));
                });
                if (degree[s] < k) { degree[s] = k; active.remove(s); sample_mode.remove(s); next_frontier.insert(s); }
                if (degree[s] - k >= sample_threshold) { samplers[s].reset(degree[s]- k); }
                else { sample_mode.remove(s);}
            });
            std::swap(frontier, next_frontier);
        }
    }
    return degree;
}

} // namespace toggle
