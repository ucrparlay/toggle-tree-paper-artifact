// This code is part of the project "Theoretically Efficient Parallel Graph
// Algorithms Can Be Fast and Scalable", presented at Symposium on Parallelism
// in Algorithms and Architectures, 2018.
// Copyright (c) 2018 Laxman Dhulipala, Guy Blelloch, and Julian Shun
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all  copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "gbbs/gbbs.h"

namespace gbbs {

template <class W>
struct BFS_F {
    uintE* Level;
    uintE cur_level;
    BFS_F(uintE* _Level, uintE _cur) : Level(_Level), cur_level(_cur) {}
    inline bool update(uintE s, uintE d, W w) {
        if (Level[d] == UINT_E_MAX) {
            Level[d] = cur_level;
            return 1;
        } 
        else return 0;
    }
    inline bool updateAtomic(uintE s, uintE d, W w) {
        return gbbs::atomic_compare_and_swap(&Level[d], UINT_E_MAX, cur_level);
    }
    inline bool cond(uintE d) { return (Level[d] == UINT_E_MAX); }
};

template <class Graph>
inline sequence<uintE> BFS(Graph& G, uintE src) {
    using W = typename Graph::weight_type;
    auto Level = sequence<uintE>::from_function(G.n, [&](size_t i) { return UINT_E_MAX; });
    Level[src] = 0;
    vertexSubset Frontier(G.n, src);
    uintE cur = 1;
    size_t reachable = 0;
    while (!Frontier.isEmpty()) {
        reachable += Frontier.size();
        Frontier = edgeMap(G, Frontier, BFS_F<W>(Level.begin(), cur), -1, sparse_blocked | dense_parallel);
        cur++;
    }
    return Level;
}

} // namespace gbbs