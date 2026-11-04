// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <iterator>
#include <limits>
#include <type_traits>
#include "macros.h"

namespace toggle {

template <class T>
constexpr T atomic_read(T& address) noexcept {
    #if defined(TOGGLE_TREE_WEAK_MEMORY)
        return std::atomic_ref<T>(address).load(std::memory_order_relaxed);
    #else
        return address;
    #endif
}

template <class T>
constexpr void atomic_write(T& address, auto value) noexcept {
    #if defined(TOGGLE_TREE_WEAK_MEMORY)
        std::atomic_ref<T>(address).store(value, std::memory_order_relaxed);
    #else
        address = value;
    #endif
}

template <class T>
constexpr bool compare_and_swap(T& address, T& old_value, auto new_value) noexcept {
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        return std::atomic_ref<T>(address).compare_exchange_strong(old_value, T(new_value), std::memory_order_relaxed, std::memory_order_relaxed);
    #else
        if constexpr (std::is_floating_point_v<T>) {
            T desired = T(new_value);
            return __atomic_compare_exchange(&address, &old_value, &desired, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        } else {
            return __atomic_compare_exchange_n(&address, &old_value, T(new_value), false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        }
    #endif
}

template <class T>
constexpr bool compare_and_set(T& address, auto old_value, auto new_value) noexcept {
    T old = T(old_value);
    if (atomic_read(address) != old) return false;
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        return std::atomic_ref<T>(address).compare_exchange_strong(old, T(new_value), std::memory_order_relaxed, std::memory_order_relaxed);
    #else
        if constexpr (std::is_floating_point_v<T>) {
            T desired = T(new_value);
            return __atomic_compare_exchange(&address, &old, &desired, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        } else {
            return __atomic_compare_exchange_n(&address, &old, T(new_value), false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        }
    #endif
}

template <class T>
constexpr T fetch_and_add(T& address, auto value) noexcept {
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        return std::atomic_ref<T>(address).fetch_add(T(value), std::memory_order_relaxed);
    #else
        return __atomic_fetch_add(&address, T(value), __ATOMIC_RELAXED);
    #endif
}

template <class T>
constexpr T fetch_and_sub(T& address, auto value) noexcept {
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        return std::atomic_ref<T>(address).fetch_sub(T(value), std::memory_order_relaxed);
    #else
        return __atomic_fetch_sub(&address, T(value), __ATOMIC_RELAXED);
    #endif
}

template <class T>
constexpr T fetch_and_or(T& address, auto value) noexcept {
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        return std::atomic_ref<T>(address).fetch_or(T(value), std::memory_order_relaxed);
    #else
        return __atomic_fetch_or(&address, T(value), __ATOMIC_RELAXED);
    #endif
}

template <class T>
constexpr T fetch_and_and(T& address, auto value) noexcept {
    #ifdef TOGGLE_TREE_WEAK_MEMORY
        return std::atomic_ref<T>(address).fetch_and(T(value), std::memory_order_relaxed);
    #else
        return __atomic_fetch_and(&address, T(value), __ATOMIC_RELAXED);
    #endif
}

template <class T>
constexpr bool write_min(T& address, auto value) noexcept {
    T old_value = atomic_read(address);
    while (value < old_value && !compare_and_swap(address, old_value, T(value)));
    return value < old_value;
}

template <class T>
constexpr bool write_max(T& address, auto value) noexcept {
    T old_value = atomic_read(address);
    while (value > old_value && !compare_and_swap(address, old_value, T(value)));
    return value > old_value;
}

template <class Sequence>
constexpr typename Sequence::value_type find(Sequence& parent, typename Sequence::value_type i) noexcept {
    using T = typename Sequence::value_type;
    T pi = atomic_read(parent[i]);
    T ppi = atomic_read(parent[pi]);
    if (pi == ppi) return pi;
    while (pi != ppi) { pi = ppi; ppi = atomic_read(parent[ppi]); }
    atomic_write(parent[i], pi);
    return pi;
}

template <class Sequence>
constexpr typename Sequence::value_type unite(Sequence& parent, typename Sequence::value_type i, typename Sequence::value_type j) noexcept {
    using T = typename Sequence::value_type;
    auto find_halve = [&](T x) {
        while (true) {
            T px = atomic_read(parent[x]);
            T ppx = atomic_read(parent[px]);
            if (px == ppx) return px;
            atomic_write(parent[x], ppx);
            x = ppx;
        }
    };
    while (true) {
        i = find_halve(i);
        j = find_halve(j);
        if (i == j) return std::numeric_limits<T>::max();
        if (i < j) { T t = i; i = j; j = t; }
        if (compare_and_set(parent[i], i, j)) return i;
    }
}

} // namespace toggle
