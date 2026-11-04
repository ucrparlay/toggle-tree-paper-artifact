#pragma once
#include <toggle/toggle.h>
using namespace parlay;
#include "hashbag.h"

template <class Graph>
toggle::Array<uint32_t> KCore(Graph& G) {
    auto coreness = toggle::Array<uint32_t>(G.n(), 0);
    auto active = toggle::IndexSet(G.n(), true);
    auto frontier = hashbag<uint32_t>(G.n());
    auto frt = toggle::Array<uint32_t>(G.n(), 0);
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
        auto frt_size = frontier.pack_into(frt);
        if (use_reduce<0 && frt_size == 0) { use_reduce++; }
        while (frt_size != 0) {
            toggle::parallel_for(0, frt_size, [&](uint32_t i) {
                uint32_t s = frt[i];
                coreness[s] = k;
                toggle::parallel_for(G.offset(s), G.offset(s + 1), [&](size_t j) {
                    uint32_t d = G.edge(j);
                    if (active.contains(d)) {
                        if (toggle::fetch_and_sub(degree[d], 1) == k+1) {
                            active.remove(d); frontier.insert(d);
                        }
                    }
                }, 256);
            }, 1);
            frt_size = frontier.pack_into(frt);
        }
    }
    
    return coreness;
}