// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "../parallel/parallel.h"
#include "../parallel/atomic.h"
#include "../parallel/word.h"
#include "array.h"

namespace toggle {

struct IndexSet {
    toggle::Array<toggle::Array<uint64_t>> toggle;
    uint64_t granularity;
    static constexpr uint64_t word_id(uint32_t layer, uint64_t index) noexcept { return index >> (6*(layer+1)); }
    static constexpr uint64_t word_offset(uint32_t layer, uint64_t index) noexcept { return 1ULL << ((index>>(6*layer)) & 63); }
    static constexpr uint64_t offset_index(uint32_t layer, uint64_t offset) noexcept { return (offset << (6*layer)); }

    IndexSet(uint64_t n, bool init_value) noexcept {
        granularity = init_value ? 64 : 8;
        uint64_t length = (n + 63) >> 6;
        uint32_t layers = 1;
        while (length > 1) { length = (length + 63) >> 6; layers++; }
        toggle = toggle::Array<toggle::Array<uint64_t>>(layers);
        length = n;
        for (uint32_t layer = 0; layer < toggle.size(); layer++) {
            toggle[layer] = toggle::Array<uint64_t>((length + 63) >> 6, init_value ? UINT64_MAX : 0);
            if (init_value && (length & 63)) toggle[layer][toggle[layer].size()-1] = (1ULL << (length & 63)) - 1;
            length = (length + 63) >> 6;
        }
    }

    bool empty() noexcept { return !toggle::atomic_read(toggle[toggle.size()-1][0]); }
    bool contains(uint64_t index) noexcept { return toggle::atomic_read(toggle[0][word_id(0, index)]) & word_offset(0, index); }
    bool insert(uint64_t index) noexcept {
        uint64_t offset = word_offset(0, index);
        uint64_t old_word = toggle::fetch_and_or(toggle[0][word_id(0, index)], offset);
        if (old_word) return !(old_word & offset);
        for (uint32_t layer = 1; layer < toggle.size(); layer++) {
            if (toggle::fetch_and_add(toggle[layer][word_id(layer, index)], word_offset(layer, index))) return true;
        }
        return true;
    }
    bool remove(uint64_t index) noexcept {
        uint64_t offset = word_offset(0, index);
        uint64_t old_word = toggle::fetch_and_and(toggle[0][word_id(0, index)], ~offset);
        if (old_word != offset) return old_word & offset;
        for (uint32_t layer = 1; layer < toggle.size(); layer++) {
            if (toggle::fetch_and_sub(toggle[layer][word_id(layer, index)], word_offset(layer, index)) != word_offset(layer, index)) return true;
        }
        return true;
    }
    void prefetch(uint64_t index) noexcept { __builtin_prefetch(toggle[0].data_ + word_id(0, index), 1, 1); }
    void set_granularity(uint64_t new_granularity) noexcept { granularity = new_granularity; }

    template <bool Consume, class F>
    void for_each(uint32_t layer, uint64_t index, F&& f) noexcept {
        // Each slot is operated by only one thread. Atomic operations are unnecessary.
        uint64_t word = toggle[layer][word_id(layer, index)];
        if constexpr (Consume) toggle[layer][word_id(layer, index)] = 0;
        if (layer == 0) {
            toggle::parallel_for(0, toggle::pop_count(word), [&](uint64_t i) {
                f(index + toggle::pop_offset(i, word)); 
            }, granularity);
        }
        else {
            toggle::parallel_for(0, toggle::pop_count(word), [&](uint64_t i) {
                for_each<Consume>(layer - 1, index + offset_index(layer, toggle::pop_offset(i, word)), f);
            }, 1);
        }
    }
    template <class F>
    void for_each(F&& f) noexcept {
        if (empty()) return;
        for_each<false>(toggle.size()-1, 0, f);
    }
    template <class F>
    void consume_each(F&& f) noexcept {
        if (empty()) return;
        for_each<true>(toggle.size()-1, 0, f);
    }

    template <bool Consume, class F>
    void for_range(uint32_t layer, uint64_t index, uint64_t l, uint64_t r, F&& f) noexcept {
        uint64_t word = toggle[layer][word_id(layer, index)];
        word &= (
            (UINT64_MAX << (l > index ? (l - index) / offset_index(layer, 1) : 0)) &
            (UINT64_MAX >> (r < index + offset_index(layer, 64) ? (index + offset_index(layer, 64) - r) / offset_index(layer, 1) : 0))
        );
        if (layer == 0) {
            toggle::parallel_for(0, toggle::pop_count(word), [&](uint64_t i) {
                uint64_t leaf = index + toggle::pop_offset(i, word);
                f(leaf);
                if constexpr (Consume) { remove(leaf); }
            }, granularity);
        }
        else {
            toggle::parallel_for(0, toggle::pop_count(word), [&](uint64_t i) {
                for_range<Consume>(layer - 1, index + offset_index(layer, toggle::pop_offset(i, word)), l, r, f);
            }, 1);
        }
    }
    template <class F>
    void for_range(uint64_t l, uint64_t r, F&& f) noexcept {
        if (empty() || l >= r) return;
        for_range<false>(toggle.size()-1, 0, l, r, f); return;
    }
    template <class F>
    void consume_range(uint64_t l, uint64_t r, F&& f) noexcept {
        if (empty() || l >= r) return;
        for_range<true>(toggle.size()-1, 0, l, r, f); return;
    }

    template<auto Combine, auto Identity, class F>
    decltype(Identity) reduce(uint32_t layer, uint64_t index, F&& f) noexcept {
        using T = decltype(Identity);
        uint64_t word = toggle[layer][word_id(layer, index)];
        uint64_t num_bits = toggle::pop_count(word);
        T aggregate = Identity;
        if (layer == 0) {
            for (uint64_t i = 0; i < num_bits; i++) {
                aggregate = Combine(aggregate, f(index + toggle::pop_offset(i, word)));
            }
        }
        else if (layer == 1) {
            for (uint64_t i = 0; i < num_bits; i++) {
                uint64_t child_index = index + offset_index(layer, toggle::pop_offset(i, word));
                uint64_t child_word = toggle[layer - 1][word_id(layer - 1, child_index)];
                uint64_t child_num_bits = toggle::pop_count(child_word);
                for (uint64_t j = 0; j < child_num_bits; j++) {
                    uint64_t grand_child_index = child_index + offset_index(layer - 1, toggle::pop_offset(j, child_word));
                    aggregate = Combine(aggregate, f(grand_child_index));
                }
            }
        }
        else {
            T aggregates[64];
            toggle::parallel_for(0, num_bits, [&](uint64_t i){
                aggregates[i] = reduce<Combine, Identity>(layer - 1, index + offset_index(layer, toggle::pop_offset(i, word)), f);
            },1);
            for (uint64_t i = 0; i < num_bits; i++) aggregate = Combine(aggregate, aggregates[i]);
        }
        return aggregate;
    }
    template<auto Combine, auto Identity, class F>
    decltype(Identity) reduce(F&& f) noexcept {
        if (empty()) return Identity;
        return reduce<Combine, Identity>(uint32_t(toggle.size()-1), 0, f);
    }

    // This is also a traversal. No update is allowed during the process.
    uint64_t reduce_size(uint32_t layer, uint64_t index) noexcept {
        uint64_t word = toggle[layer][word_id(layer, index)];
        uint64_t num_bits = toggle::pop_count(word);
        uint64_t aggregate = 0;
        if (layer == 0) {
            return num_bits;
        }
        else if (layer == 1) {
            for (uint64_t i = 0; i < num_bits; i++) {
                aggregate += toggle::pop_count(
                    toggle[0][word_id(0, index + offset_index(layer, toggle::pop_offset(i, word)))]
                );
            }
        }
        else if (layer == 2) {
            for (uint64_t i = 0; i < num_bits; i++) {
                uint64_t child_index = index + offset_index(layer, toggle::pop_offset(i, word));
                uint64_t child_word = toggle[layer - 1][word_id(layer - 1, child_index)];
                uint64_t child_num_bits = toggle::pop_count(child_word);
                for (uint64_t j = 0; j < child_num_bits; j++) {
                    uint64_t grand_child_index = child_index + offset_index(layer - 1, toggle::pop_offset(j, child_word));
                    uint64_t grand_child_word = toggle[layer - 2][word_id(layer - 2, grand_child_index)];
                    aggregate += toggle::pop_count(grand_child_word);
                }
            }
        }
        else {
            uint64_t aggregates[64];
            toggle::parallel_for(0, num_bits, [&](uint64_t i){
                aggregates[i] = reduce_size(layer - 1, index + offset_index(layer, toggle::pop_offset(i, word)));
            }, 1);
            for (uint64_t i = 0; i < num_bits; i++) aggregate += aggregates[i];
        }
        return aggregate;
    }
    uint64_t reduce_size() noexcept { return empty() ? 0 : reduce_size(uint32_t(toggle.size()-1), 0); }

    uint64_t sample() noexcept {
        if (empty()) return UINT64_MAX;
        static thread_local uint64_t dice = toggle::worker_id() + 1;
        auto roll_dice = [&]() { dice ^= dice << 13; dice ^= dice >> 7; dice ^= dice << 17; return dice; };
        uint64_t index = 0, word = 0;
        for (uint32_t layer = uint32_t(toggle.size()-1); ; layer--){
            word = toggle[layer][word_id(layer, index)];
            index += offset_index(layer, pop_offset(roll_dice() % pop_count(word), word));
            if (layer == 0) return index;
        }
    }
};

} // namespace toggle
