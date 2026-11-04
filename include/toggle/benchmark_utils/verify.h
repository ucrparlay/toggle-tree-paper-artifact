// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(TOGGLE_TREE)
    #include "../parallel/parallel.h"
#endif

namespace toggle {

// Public API

inline std::string extract_graph_name(std::string path);

template <class T>
std::vector<T> seq_random_permutation(uint64_t n, uint64_t seed = 0);

template <class Sequence>
bool availability(Sequence& result, double proportion);

inline std::vector<uint32_t> get_sources(std::string sources_path, std::string graph_name, uint32_t num_sources);

template <class Sequence>
void process_result(double t, Sequence& result, std::string csvpath, std::string row, std::string column, std::string dumppath = "");

// Implementation

namespace internal {

constexpr uint64_t hash64(uint64_t value) {
    value = value * 3935559000370003845ULL + 2691343689449507681ULL;
    value ^= value >> 21;
    value ^= value << 37;
    value ^= value >> 4;
    value *= 4768777513237032717ULL;
    value ^= value << 20;
    value ^= value >> 41;
    value ^= value << 5;
    return value;
}

template <class Sequence>
std::string get_hash(Sequence& result) {
    using T = typename Sequence::value_type;
    static_assert(sizeof(T) <= sizeof(uint64_t));
    static_assert(std::is_trivially_copyable_v<T>);
    auto to_bits = [](T& x) -> uint64_t {
        T y = x;
        if constexpr (std::is_floating_point_v<T>) { if (y == 0) y = 0; }
        uint64_t bits = 0;
        std::memcpy(&bits, &y, sizeof(T));
        return bits;
    };
    auto reduce_mix = [&](auto&& self, uint64_t l, uint64_t r) -> uint64_t {
        if (l == r) return 0;
        if (r - l <= 1024) {
            uint64_t aggregate = hash64(to_bits(result[l])) + hash64(l);
            for (uint64_t i = l+1; i < r; ++i) { aggregate ^= (hash64(to_bits(result[i])) + hash64(i)); }
            return aggregate;
        }
        uint64_t m = l + (r - l) / 2, L = 0, R = 0;
        auto lf = [&]() { L = self(self, l, m); };
        auto rf = [&]() { R = self(self, m, r); };
        #if defined(TOGGLE_TREE)
            toggle::parallel_do(lf, rf);
        #elif defined(PARLAY_PARALLEL_H_)
            parlay::parallel_do(lf, rf);
        #else
            lf(); rf();
        #endif
        return L ^ R;
    };
    uint64_t result_mix = reduce_mix(reduce_mix, 0, result.size());
    uint32_t folded = uint32_t(result_mix) ^ uint32_t(result_mix >> 32);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(8) << folded;
    return oss.str();
}

using CSV = std::vector<std::vector<std::string>>;
inline CSV read_csv(const std::string& path) {
    CSV table;
    std::ifstream ifs(path);
    if (!ifs.good()) return table;
    std::string line;
    while (std::getline(ifs, line)) {
        std::vector<std::string> row;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            row.push_back(cell);
        }
        table.push_back(row);
    }
    return table;
}

inline void write_csv(const std::string& path, const CSV& table) {
    std::ofstream ofs(path);
    for (const auto& row : table) {
        for (uint64_t i = 0; i < row.size(); i++) {
            if (i) ofs << ',';
            ofs << row[i];
        }
        ofs << '\n';
    }
}

inline void update_csv_cell(const std::string& path, const std::string& graph, const std::string& column, const std::string& value) {
    CSV table = read_csv(path);
    if (table.empty()) { table.push_back({"graph", column}); }
    auto& header = table[0];
    uint64_t col = 0;
    for (; col < header.size(); col++) { if (header[col] == column) break; }
    if (col == header.size()) { header.push_back(column); }
    for (uint64_t i = 1; i < table.size(); i++) {
        if (table[i].size() < header.size()) {
            table[i].resize(header.size(), "");
        }
    }
    uint64_t row = 1;
    for (; row < table.size(); row++) { if (!table[row].empty() && table[row][0] == graph) break; }
    if (row == table.size()) {
        table.push_back(std::vector<std::string>(header.size(), ""));
        table[row][0] = graph;
    }
    table[row][col] = value;
    write_csv(path, table);
}

} // namespace internal

inline std::string extract_graph_name(std::string path) {
    std::string name = std::string(path).substr(0, std::string(path).find_last_of('.'));
    if (name.find_last_of('/') != std::string::npos) { name = name.substr(name.find_last_of('/') + 1); }
    return name;
}

template <class T>
std::vector<T> seq_random_permutation(uint64_t n, uint64_t seed) {
    static_assert(std::is_integral_v<T>);
    auto random_log2_up = [](uint64_t value) noexcept {
        uint64_t result = 0;
        for (uint64_t remaining = value - 1; remaining != 0; remaining >>= 1) { ++result; }
        return result;
    };
    auto seq_random_shuffle = [](std::vector<T>& values, uint64_t first, uint64_t last, uint64_t state = 0) noexcept {
        if (last - first < 2) return;
        for (uint64_t i = last - first - 1; i != 0; --i) {
            std::swap(values[first + i], values[first + internal::hash64(i + state) % (i + 1)]);
        }
    };
    if (n < 8192) {
        std::vector<T> input(n);
        for (uint64_t i = 0; i < n; ++i) { input[i] = static_cast<T>(i); }
        seq_random_shuffle(input, 0, n, seed);
        return input;
    }
    const uint64_t num_buckets = uint64_t{1} << ((n < (uint64_t{1} << 27)) ? (random_log2_up(n) - 7) / 2 : random_log2_up(n) - 17);
    std::vector<uint64_t> counts(num_buckets, 0);
    for (uint64_t i = 0; i < n; ++i) {
        ++counts[internal::hash64(i + seed) & (num_buckets - 1)];
    }
    std::vector<uint64_t> bucket_offsets(num_buckets + 1);
    uint64_t offset = 0;
    for (uint64_t bucket = 0; bucket < num_buckets; ++bucket) {
        bucket_offsets[bucket] = offset;
        offset += counts[bucket];
        counts[bucket] = offset - counts[bucket];
    }
    bucket_offsets[num_buckets] = offset;
    std::vector<T> output(n);
    for (uint64_t i = 0; i < n; ++i) {
        output[counts[internal::hash64(i + seed) & (num_buckets - 1)]++] = static_cast<T>(i);
    }
    for (uint64_t bucket = 0; bucket < num_buckets; ++bucket) {
        seq_random_shuffle(output, bucket_offsets[bucket], bucket_offsets[bucket + 1], internal::hash64(internal::hash64(bucket + seed)));
    }
    return output;
}

template <class Sequence>
bool availability(Sequence& result, double proportion) {
    using T = typename Sequence::value_type;
    auto recuce_sum = [&](auto&& self, uint64_t l, uint64_t r) -> uint64_t {
        if (r - l <= 1024) {
            uint64_t aggregate = 0;
            for (uint64_t i = l; i < r; ++i) { aggregate += (result[i]!=std::numeric_limits<T>::max()); }
            return aggregate;
        }
        uint64_t m = l + (r - l) / 2, L = 0, R = 0;
        auto lf = [&]() { L = self(self, l, m); };
        auto rf = [&]() { R = self(self, m, r); };
        #if defined(TOGGLE_TREE)
            toggle::parallel_do(lf, rf);
        #elif defined(PARLAY_PARALLEL_H_)
            parlay::parallel_do(lf, rf);
        #else
            lf(); rf();
        #endif
        return L + R;
    };
    uint64_t tot = recuce_sum(recuce_sum, 0, result.size());
    return (double)tot/(double)result.size() >= proportion;
}

inline std::vector<uint32_t> get_sources(std::string sources_path, std::string graph_name, uint32_t num_sources) {
    std::vector<uint32_t> sources;
    while (true) {
        auto table = toggle::internal::read_csv(sources_path);
        if (table.empty()) break;
        auto row = std::find_if(table.begin() + 1, table.end(), [&](auto& r) { return !r.empty() && r[0] == graph_name; });
        if (row == table.end() || row->size() < num_sources + 1) break;
        for (uint32_t i = 1; i <= num_sources; i++) {
            const std::string& cell = (*row)[i];
            uint64_t s = 0;
            auto [end, error] = std::from_chars(cell.data(), cell.data() + cell.size(), s);
            if (error != std::errc() || end != cell.data() + cell.size()) break;
            sources.push_back(s);
        }
        return sources;
    }
    std::cout << "Warning: Failed in loading random sources.\n";
    std::cout << "         Please run \"./generate.sh\" under \"toggle-tree/dataset/generate_source/\" first.\n";
    std::cout << "         Falling back to source = [0,1,2,...].\n";
    sources.clear();
    for (uint32_t i = 0; i < num_sources; i++) { sources.push_back(i); }
    return sources;
}

template <class Sequence>
void process_result(double t, Sequence& result, std::string csvpath, std::string row, std::string column, std::string dumppath) {
    using T = typename Sequence::value_type;
    std::cout << "Running Time: " << t << " sec\n";
    auto reduce_max = [&](auto&& self, uint64_t l, uint64_t r) -> T {
        if (r - l <= 1024) {
            T aggregate = std::numeric_limits<T>::min();
            for (uint64_t i = l; i < r; ++i) { if(result[i]!=std::numeric_limits<T>::max()) aggregate = std::max<T>(result[i], aggregate); }
            return aggregate;
        }
        uint64_t m = l + (r - l) / 2;
        T L, R;
        auto lf = [&]() { L = self(self, l, m); };
        auto rf = [&]() { R = self(self, m, r); };
        #if defined(TOGGLE_TREE)
            toggle::parallel_do(lf, rf);
        #elif defined(PARLAY_PARALLEL_H_)
            parlay::parallel_do(lf, rf);
        #else
            lf(); rf();
        #endif
        return (L == std::numeric_limits<T>::max()) ? R : std::max<T>(L, R);
    };
    T result_max = reduce_max(reduce_max, 0, result.size());
    std::cout << "Maximum Res : " << result_max << '\n';
    std::string res_hash = internal::get_hash(result);
    std::cout << "Verify  Hash: " << res_hash << '\n';
    internal::update_csv_cell(csvpath + "/benchmark.csv", row, column, std::to_string(t));
    internal::update_csv_cell(csvpath + "/verify.csv",    row, column, res_hash);
    if (dumppath != "disabled" && dumppath != "") {
        std::ofstream out(dumppath, std::ios::out|std::ios::trunc);
        for(uint64_t i=0;i<result.size();++i){ out<<result[i]<<","; }
        out<<"\n";
    }
}

} // namespace toggle
