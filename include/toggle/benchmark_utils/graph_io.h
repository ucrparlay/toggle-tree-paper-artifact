// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include "../parallel/macros.h"
#include "../parallel/parallel.h"
#include "../parallel/word.h"
#include "../data_structures/array.h"
#include "../data_structures/csr.h"
#include "../primitives/combine.h"
#include "../primitives/reduce.h"
#include "../primitives/pack.h"
#include "../primitives/random.h"
#include "../primitives/sort.h"
#ifndef TOGGLE_TREE_WEAK_MEMORY
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace toggle {

template <bool Symmetrized, class T>
CSR<Symmetrized, T> read_graph(std::string filename);

template <bool Symmetrized, class T>
void write_graph(CSR<Symmetrized, T>& G, std::string filename);

template <bool Symmetrized>
void make_random_weight(CSR<Symmetrized, uint32_t>& G, int32_t r=-1);

namespace internal {

template <class Graph>
void fill_edges_from_file(Graph& G, const char* edge_data, bool file_weighted) {
    if (!file_weighted && !Graph::weighted) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(edge_data);
        toggle::parallel_for(0, G.m_, [&](uint64_t i){ G.out_edges[i] = src[i]; }, 1024);
    }
    else if (!file_weighted && Graph::weighted) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(edge_data);
        constexpr uint32_t one = std::is_floating_point_v<typename Graph::weight_type> ? 0x3F800000 : 1;
        toggle::parallel_for(0, G.m_, [&](uint64_t i){ G.out_edges[i] = toggle::glue32(one, src[i]); }, 1024);
    }
    else if (file_weighted && !Graph::weighted) {
        const uint64_t* src = reinterpret_cast<const uint64_t*>(edge_data);
        toggle::parallel_for(0, G.m_, [&](uint64_t i){ G.out_edges[i] = toggle::hi32(src[i]); }, 1024);
    }
    else if (file_weighted && Graph::weighted) {
        const uint64_t* src = reinterpret_cast<const uint64_t*>(edge_data);
        toggle::parallel_for(0, G.m_, [&](uint64_t i){ G.out_edges[i] = toggle::glue32(toggle::lo32(src[i]), toggle::hi32(src[i])); }, 1024);
    }
}

template <class Graph>
bool is_file_symmetrized(Graph& G) {
    uint64_t aggregate = toggle::reduce<toggle::sum, uint64_t(0)>(0, G.n(), [&](uint64_t u) {
        return toggle::reduce<toggle::sum, uint64_t(0)>(G.out_offsets[u], G.out_offsets[u + 1], [&](uint64_t i) {
            uint64_t v = uint32_t(G.out_edges[i]);
            return toggle::hash64(toggle::glue32(u, v)) - toggle::hash64(toggle::glue32(v, u));
        });
    });
    return aggregate == 0;
}

template <class Graph>
void make_in_edges(Graph& G) {
    using edge_type = typename Graph::edge_type;
    struct Entry { uint32_t dst; edge_type back; };
    const uint64_t n = G.n_, m = G.m_;
    std::unique_ptr<Entry[]> edgelist(new Entry[m]);
    toggle::parallel_for(0, n, [&](uint32_t u) {
        toggle::parallel_for(G.out_offsets[u], G.out_offsets[u + 1], [&](uint64_t i) {
            edge_type e = G.out_edges[i];
            edge_type back;
            if constexpr (Graph::weighted) { back = (e & ~uint64_t(0xFFFFFFFFULL)) | uint64_t(u); }
            else { back = u; }
            edgelist[i] = Entry{uint32_t(e), back};
        }, 256);
    }, 1);
    toggle::parallel_sort(edgelist.get(), edgelist.get() + m, [](const Entry& a, const Entry& b) {
        if (a.dst != b.dst) return a.dst < b.dst;
        return uint32_t(a.back) < uint32_t(b.back);
    });
    G.in_offsets = toggle::Array<uint64_t>(n + 1, m);
    G.in_edges = toggle::Array<edge_type>(m);
    toggle::parallel_for(0, m, [&](uint64_t i) {
        G.in_edges[i] = edgelist[i].back;
        if (i == 0 || edgelist[i].dst != edgelist[i - 1].dst) { G.in_offsets[edgelist[i].dst] = i; }
    }, 1024);
    toggle::parallel_for(0, n + 1, [&](uint64_t i) {
        uint32_t u = n - i;
        if (G.in_offsets[u] != m) return;
        for (uint32_t v = u + 1; v <= n; v++) {
            uint64_t r = G.in_offsets[v];
            if (r != m) { G.in_offsets[u] = r; return; }
        }
    }, 1024);
}

template <class Graph>
void make_symmetrized(Graph& G) {
    using edge_type = typename Graph::edge_type;
    using Entry = std::conditional_t<Graph::weighted, std::pair<uint64_t, uint32_t>, std::pair<uint32_t, uint32_t>>;
    auto key_of = [](const Entry& x) -> uint64_t { if constexpr (Graph::weighted) { return x.first; } else { return toggle::glue32(x.first, x.second); } };
    const uint64_t n = G.n_, m = G.m_;
    toggle::Array<Entry> edgelist(2 * m);
    toggle::parallel_for(0, n, [&](uint32_t u) {
        toggle::parallel_for(G.out_offsets[u], G.out_offsets[u + 1], [&](uint64_t i) {
            edge_type e = G.out_edges[i];
            uint32_t v = uint32_t(e);
            if constexpr (Graph::weighted) {
                uint32_t w = uint32_t(e >> 32);
                edgelist[2 * i] = Entry{toggle::glue32(u, v), w};
                edgelist[2 * i + 1] = Entry{toggle::glue32(v, u), w};
            }
            else {
                edgelist[2 * i] = Entry{u, v};
                edgelist[2 * i + 1] = Entry{v, u};
            }
        }, 256);
    }, 1);
    if constexpr (Graph::weighted) {
        toggle::parallel_sort(edgelist, [](const Entry& a, const Entry& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second > b.second;
        });
    }
    else { toggle::parallel_sort(edgelist); }
    toggle::Array<Entry> kept = toggle::pack<Entry>(0, 2 * m,
        [&](uint64_t i) {
            uint64_t key = key_of(edgelist[i]);
            if (uint32_t(key) == uint32_t(key >> 32)) return false;
            return i + 1 == 2 * m || key != key_of(edgelist[i + 1]);
        },
        [&](uint64_t i) { return edgelist[i]; }
    );
    const uint64_t new_m = kept.size();
    G.m_ = new_m;
    G.out_edges = toggle::Array<edge_type>(new_m, [&](uint64_t i) -> edge_type {
        if constexpr (Graph::weighted) { return uint64_t(uint32_t(kept[i].first)) | (uint64_t(kept[i].second) << 32); }
        else { return kept[i].second; }
    });
    G.out_offsets = toggle::Array<uint64_t>(n + 1, new_m);
    toggle::parallel_for(0, new_m, [&](uint64_t i) {
        uint32_t src = uint32_t(key_of(kept[i]) >> 32);
        if (i == 0 || src != uint32_t(key_of(kept[i - 1]) >> 32)) { G.out_offsets[src] = i; }
    }, 1024);
    toggle::parallel_for(0, n + 1, [&](uint64_t i) {
        uint32_t u = n - i;
        if (G.out_offsets[u] != new_m) return;
        for (uint32_t v = u + 1; v <= n; v++) {
            uint64_t r = G.out_offsets[v];
            if (r != new_m) { G.out_offsets[u] = r; return; }
        }
    }, 1024);
}

template <class F>
uint64_t shared_sum(uint64_t n, F&& f) {
    uint64_t copies = 16, block = 4096, chunk = block * toggle::num_workers();
    uint64_t total = 0;
    for (uint64_t start = 0; start < n; start += chunk) {
        uint64_t end = std::min(start + chunk, n), num_blocks = (end - start + block - 1) / block;
        total += toggle::reduce<toggle::sum, uint64_t(0)>(0, num_blocks * copies, [&](uint64_t k) {
            uint64_t lo = start + k % num_blocks * block, hi = std::min(lo + block, end);
            return toggle::reduce<toggle::sum, uint64_t(0)>(lo, hi, f);
        }, 1);
    }
    return total;
}

template <class Offsets, class Edges>
void make_sorted(Offsets& offsets, Edges& edges, uint64_t n) {
    using edge_type = std::remove_reference_t<decltype(edges[0])>;
    auto is_sorted = [&]() {
        uint64_t all_descents = shared_sum(offsets[n], [&](uint64_t i) {
            return uint64_t(i > 0 && uint32_t(edges[i - 1]) > uint32_t(edges[i]));
        });
        uint64_t boundary_descents = shared_sum(n, [&](uint64_t s) {
            uint64_t i = offsets[s];
            return uint64_t(i > 0 && i < offsets[s + 1] && uint32_t(edges[i - 1]) > uint32_t(edges[i]));
        });
        return all_descents == boundary_descents;
    };
    while (!is_sorted()) {
        std::cout << "Warning: graph file is not sorted.\n";
        toggle::parallel_for(0, n, [&](uint64_t u) {
            if constexpr (std::is_same_v<edge_type, uint32_t>) {
                std::sort(edges.begin() + offsets[u], edges.begin() + offsets[u + 1]);
            }
            else {
                std::sort(edges.begin() + offsets[u], edges.begin() + offsets[u + 1], [](edge_type a, edge_type b) { return uint32_t(a) < uint32_t(b); });
            }
        }, 1);
    }
}

template <class Graph>
void read_binary_format(Graph& G, std::string filename) {
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        using edge_type = typename Graph::edge_type;
        std::ifstream in(filename.c_str(), std::ios::binary);
        if (!in) { std::cerr << "Error: Failed in opening file " << filename << "\n"; std::abort(); }
        auto parallel_read = [&](uint64_t file_offset, void* dst, uint64_t size) {
            if (size == 0) return;
            constexpr uint64_t num_readers = 192;
            constexpr uint64_t alignment = 4096;
            uint64_t chunk_size = (size + num_readers - 1) / num_readers;
            chunk_size = (chunk_size + alignment - 1) / alignment * alignment;
            uint64_t num_chunks = (size + chunk_size - 1) / chunk_size;
            auto* output = static_cast<char*>(dst);
            toggle::parallel_for(0, num_chunks, [&](uint64_t chunk) {
                uint64_t begin = chunk * chunk_size;
                uint64_t chunk_bytes = std::min(chunk_size, size - begin);
                std::ifstream chunk_in(filename, std::ios::binary);
                if (!chunk_in) { std::cerr << "Error: Failed in opening file " << filename << "\n"; std::abort(); }
                chunk_in.seekg(static_cast<std::streamoff>(file_offset + begin));
                chunk_in.read(output + begin, static_cast<std::streamsize>(chunk_bytes));
                if (!chunk_in) { std::cerr << "Error: Failed in reading file " << filename << "\n"; std::abort(); }
            }, 1);
        };
        uint64_t header[3];
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!in) { std::cerr << "Error: Failed in reading file " << filename << "\n"; std::abort(); }
        in.close();
        G.n_ = header[0]; G.m_ = header[1];
        const uint64_t n = G.n_, m = G.m_;
        bool file_weighted = (header[2] == 24 + (n + 1) * 8 + m * 8);
        uint64_t offsets_bytes = (n + 1) * sizeof(uint64_t);
        G.out_offsets = toggle::Array<uint64_t>(n + 1);
        G.out_edges = toggle::Array<edge_type>(m);
        parallel_read(sizeof(header), G.out_offsets.begin(), offsets_bytes);
        uint64_t edge_bytes = m * (file_weighted ? 8 : 4);
        std::unique_ptr<uint64_t[]> raw(new uint64_t[(edge_bytes + 7) / 8]);
        parallel_read(sizeof(header) + offsets_bytes, raw.get(), edge_bytes);
        fill_edges_from_file(G, reinterpret_cast<const char*>(raw.get()), file_weighted);
    #else
        using edge_type = typename Graph::edge_type;
        struct stat sb;
        int fd = open(filename.c_str(), O_RDONLY);
        if (fd == -1 || fstat(fd, &sb) == -1) { std::cerr << "Error: Failed in opening file " << filename << "\n"; std::abort(); }
        uint64_t len = uint64_t(sb.st_size);
        char *data = (char*)mmap(0, size_t(len), PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) { std::cerr << "Error: Failed in mapping file " << filename << "\n"; std::abort(); }
        const uint64_t* header = reinterpret_cast<const uint64_t*>(data);
        G.n_ = header[0];
        G.m_ = header[1];
        const uint64_t n = G.n_, m = G.m_;
        bool file_weighted = (header[2] == 24 + (n + 1) * 8 + m * 8);
        G.out_offsets = toggle::Array<uint64_t>(n + 1);
        G.out_edges = toggle::Array<edge_type>(m);
        const uint64_t* offsets_data = reinterpret_cast<const uint64_t*>(data + 24);
        toggle::parallel_for(0, n + 1, [&](uint64_t i){ G.out_offsets[i] = offsets_data[i]; }, 1024);
        fill_edges_from_file(G, data + 24 + (n + 1) * 8, file_weighted);
        munmap(data, len);
        close(fd);
    #endif
}

} // namespace internal

template <bool Symmetrized, class T>
CSR<Symmetrized, T> read_graph(std::string filename) {
    using Graph = CSR<Symmetrized, T>;
    using edge_type = typename Graph::edge_type;
    Graph G;
    internal::read_binary_format(G, filename.c_str());
    bool file_symmetrized = internal::is_file_symmetrized(G);
    if (file_symmetrized && Symmetrized) { }
    else if (file_symmetrized && !Symmetrized) {
        G.in_offsets = toggle::Array<uint64_t>(G.out_offsets);
        G.in_edges = toggle::Array<edge_type>(G.out_edges);
    }
    else if (!file_symmetrized && !Symmetrized) {
        internal::make_in_edges(G);
    }
    else { // !file_symmetrized && Symmetrized
        internal::make_symmetrized(G);
    }
    internal::make_sorted(G.out_offsets, G.out_edges, G.n());
    if constexpr (!Symmetrized) internal::make_sorted(G.in_offsets, G.in_edges, G.n());
    return G;
}

template <bool Symmetrized, class T>
void write_graph(CSR<Symmetrized, T>& G, std::string filename) {
    using Graph = CSR<Symmetrized, T>;
    std::ofstream out(filename, std::ios::binary);
    if (!out) { std::cerr << "Error: Cannot open output file " << filename << "\n"; std::abort(); }
    const uint64_t n = G.n(), m = G.m();
    uint64_t header[3];
    header[0] = n;
    header[1] = m;
    header[2] = 24 + (n + 1) * 8 + m * (Graph::weighted ? 8 : 4);
    out.write(reinterpret_cast<const char*>(header), 24);
    out.write(reinterpret_cast<const char*>(G.out_offsets.begin()), static_cast<std::streamsize>((n + 1) * 8));
    if constexpr (!Graph::weighted) {
        out.write(reinterpret_cast<const char*>(G.out_edges.begin()), static_cast<std::streamsize>(m * 4));
    }
    else {
        constexpr uint64_t chunk = uint64_t(1) << 26;
        toggle::Array<uint64_t> raw(std::min(m, chunk));
        for (uint64_t first = 0; first < m; first += chunk) {
            const uint64_t len = std::min(chunk, m - first);
            toggle::parallel_for(0, len, [&](uint64_t i) { uint64_t x = G.out_edges[first + i]; raw[i] = (x >> 32) | (x << 32); }, 1024);
            out.write(reinterpret_cast<const char*>(raw.begin()), static_cast<std::streamsize>(len * 8));
        }
    }
    out.close();
    if (!out) { std::cerr << "Error: Failed in writing file " << filename << "\n"; std::abort(); }
}

template <bool Symmetrized>
void make_random_weight(CSR<Symmetrized, uint32_t>& G, int32_t r) {
    uint32_t range = (r == -1) ? uint32_t(toggle::log2(G.n())) : uint32_t(1ULL << r);
    auto assign = [&](toggle::Array<uint64_t>& offsets, toggle::Array<uint64_t>& edges) {
        toggle::parallel_for(0, G.n(), [&](uint32_t u) {
            toggle::parallel_for(offsets[u], offsets[u + 1], [&](uint64_t i) {
                uint32_t v = uint32_t(edges[i]);
                uint64_t w = 1 + (toggle::hash64(toggle::glue32(std::min(u, v), std::max(u, v))) % range);
                edges[i] = uint64_t(v) | (w << 32);
            }, 256);
        }, 1);
    };
    assign(G.out_offsets, G.out_edges);
    if constexpr (!Symmetrized) { assign(G.in_offsets, G.in_edges); }
}

} // namespace toggle
