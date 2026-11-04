// SPDX-License-Identifier: MIT
#pragma once
#include <bit>
#include <cstdint>
#include <type_traits>
#include <utility>
#include "array.h"

namespace toggle {

template <bool Symmetrized = true, class T = void>
struct CSR {
    static_assert(std::is_void_v<T> || std::is_same_v<T, uint32_t> || std::is_same_v<T, float>);
    static constexpr bool symmetrized = Symmetrized;
    static constexpr bool weighted = !std::is_void_v<T>;
    using edge_type = std::conditional_t<std::is_void_v<T>, uint32_t, uint64_t>;
    using weight_type = std::conditional_t<std::is_void_v<T>, uint32_t, T>;

    uint64_t n_ = 0;
    uint64_t m_ = 0;
    toggle::Array<uint64_t> out_offsets;
    toggle::Array<edge_type> out_edges;
    toggle::Array<uint64_t> in_offsets;
    toggle::Array<edge_type> in_edges;

    uint64_t n() noexcept { return n_; }
    uint64_t m() noexcept { return m_; }

    uint64_t offset(uint64_t i) noexcept requires (Symmetrized) {
        return out_offsets[i];
    }
    template <bool Direction = true>
    uint64_t out_offset(uint64_t i) noexcept {
        if constexpr (Direction || Symmetrized) { return out_offsets[i]; }
        else { return in_offsets[i]; }
    }
    template <bool Direction = true>
    uint64_t in_offset(uint64_t i) noexcept {
        if constexpr (!Direction || Symmetrized) { return out_offsets[i]; }
        else { return in_offsets[i]; }
    }

    uint32_t edge(uint64_t i) noexcept requires (Symmetrized) {
        return uint32_t(out_edges[i]);
    }
    template <bool Direction = true>
    uint32_t out_edge(uint64_t i) noexcept {
        if constexpr (Direction || Symmetrized) { return uint32_t(out_edges[i]); }
        else { return uint32_t(in_edges[i]); }
    }
    template <bool Direction = true>
    uint32_t in_edge(uint64_t i) noexcept {
        if constexpr (!Direction || Symmetrized) { return uint32_t(out_edges[i]); }
        else { return uint32_t(in_edges[i]); }
    }

    std::pair<uint32_t, weight_type> weighted_edge(uint64_t i) noexcept requires (Symmetrized) {
        edge_type e = out_edges[i];
        if constexpr (std::is_void_v<T>) {
            return {uint32_t(e), uint32_t(1)};
        } else if constexpr (std::is_integral_v<T>) {
            return {uint32_t(e), weight_type(uint32_t(e >> 32))};
        } else {
            return {uint32_t(e), std::bit_cast<float>(uint32_t(e >> 32))};
        }
    }
    template <bool Direction = true>
    std::pair<uint32_t, weight_type> weighted_out_edge(uint64_t i) noexcept {
        edge_type e;
        if constexpr (Direction || Symmetrized) { e = out_edges[i]; }
        else { e = in_edges[i]; }
        if constexpr (std::is_void_v<T>) {
            return {uint32_t(e), uint32_t(1)};
        } else if constexpr (std::is_integral_v<T>) {
            return {uint32_t(e), weight_type(uint32_t(e >> 32))};
        } else {
            return {uint32_t(e), std::bit_cast<float>(uint32_t(e >> 32))};
        }
    }
    template <bool Direction = true>
    std::pair<uint32_t, weight_type> weighted_in_edge(uint64_t i) noexcept {
        edge_type e;
        if constexpr (!Direction || Symmetrized) { e = out_edges[i]; }
        else { e = in_edges[i]; }
        if constexpr (std::is_void_v<T>) {
            return {uint32_t(e), uint32_t(1)};
        } else if constexpr (std::is_integral_v<T>) {
            return {uint32_t(e), weight_type(uint32_t(e >> 32))};
        } else {
            return {uint32_t(e), std::bit_cast<float>(uint32_t(e >> 32))};
        }
    }
};

} // namespace toggle
