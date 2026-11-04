// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>
#include "../parallel/parallel.h"
#include "../parallel/atomic.h"
#include "../parallel/word.h"
#include "array.h"

namespace toggle {

struct NotDirty {};

template <class Dirty, class T>
struct Node {
    uint64_t flag;
    [[no_unique_address]] Dirty dirty;
    T augval;
};

template <class Sequence, bool EnableDirty = true>
struct IndexMap {
    using T = typename Sequence::value_type;
    using Dirty = std::conditional_t<EnableDirty, uint64_t, NotDirty>;
    using NodeType = Node<Dirty, T>;
    Sequence& sequence;
    toggle::Array<toggle::Array<NodeType>> tree;
    static constexpr uint64_t word_id(uint32_t layer, uint64_t index) noexcept { return index >> (6*(layer+1)); }
    static constexpr uint64_t word_offset(uint32_t layer, uint64_t index) noexcept { return 1ULL << ((index>>(6*layer)) & 63); }
    static constexpr uint64_t offset_index(uint32_t layer, uint64_t offset) noexcept { return offset << (6*layer); }

    IndexMap(Sequence& _sequence, bool init_value = !EnableDirty): sequence(_sequence) {
        uint64_t length = sequence.size();
        uint64_t node_count = (length + 63) >> 6;
        uint32_t layers = 1;
        while (node_count > 1) { node_count = (node_count + 63) >> 6; layers++; }
        tree = toggle::Array<toggle::Array<NodeType>>(layers);
        for (uint32_t layer = 0; layer < tree.size(); layer++) {
            uint64_t layer_length = length;
            if (!init_value) {
                tree[layer] = toggle::Array<NodeType>((layer_length + 63) >> 6, NodeType{0, Dirty{}, std::numeric_limits<T>::max()});
            }
            else {
                tree[layer] = toggle::Array<NodeType>((layer_length + 63) >> 6, [&](uint64_t i) {
                    uint64_t begin = i << 6;
                    uint64_t end = std::min<uint64_t>(layer_length, begin + 64);
                    T aggregate = std::numeric_limits<T>::max();
                    if (layer == 0) {
                        for (uint64_t j = begin; j < end; j++) aggregate = std::min(aggregate, sequence[j]);
                    }
                    else {
                        for (uint64_t j = begin; j < end; j++) aggregate = std::min(aggregate, tree[layer - 1][j].augval);
                    }
                    uint64_t flag = end - begin == 64 ? UINT64_MAX : (1ULL << (end - begin)) - 1;
                    return NodeType{flag, Dirty{}, aggregate};
                });
            }
            length = (length + 63) >> 6;
        }
    }
    bool empty() noexcept { return !tree[tree.size()-1][0].flag; }
    void insert(uint64_t index) noexcept requires (EnableDirty) {
        if (toggle::fetch_and_or(tree[0][word_id(0, index)].dirty, word_offset(0, index))) return;
        for (uint32_t layer = 1; layer < tree.size(); layer++) {
            if (toggle::fetch_and_add(tree[layer][word_id(layer, index)].dirty, word_offset(layer, index))) return;
        }
    }

    T repair(uint32_t layer, uint64_t index) noexcept requires (EnableDirty) {
        NodeType& node = tree[layer][word_id(layer, index)];
        uint64_t dirty = node.dirty;
        node.dirty = 0;
        node.flag |= dirty;
        T aggregate = node.augval;
        uint64_t num_bits = toggle::pop_count(dirty);
        if (layer == 0) {
            for (uint64_t i = 0; i < num_bits; i++) {
                aggregate = std::min(aggregate, sequence[index + toggle::pop_offset(i, dirty)]);
            }
        }
        else {
            T aggregates[64];
            toggle::parallel_for(0, num_bits, [&](uint64_t i) {
                aggregates[i] = repair(layer - 1, index + offset_index(layer, toggle::pop_offset(i, dirty)));
            }, 1);
            for (uint64_t i = 0; i < num_bits; i++) aggregate = std::min(aggregate, aggregates[i]);
        }
        return node.augval = aggregate;
    }
    T repair() noexcept requires (EnableDirty) {
        NodeType& root = tree[tree.size()-1][0];
        if (!toggle::atomic_read(root.dirty)) return root.augval;
        return repair(tree.size()-1, 0);
    }

    template<class F>
    uint64_t extract_min(uint32_t layer, uint64_t index, T threshold, F&& f) noexcept {
        NodeType& node = tree[layer][word_id(layer, index)];
        uint64_t word = node.flag;
        uint64_t new_word = 0;
        uint64_t count = 0;
        T aggregate = std::numeric_limits<T>::max();
        uint64_t num_bits = toggle::pop_count(word);
        if (layer == 0) {
            for (uint64_t i = 0; i < num_bits; i++) {
                uint64_t vertex = index + toggle::pop_offset(i, word);
                if (sequence[vertex] <= threshold) { f(vertex); count++; }
                else {
                    aggregate = std::min(aggregate, sequence[vertex]);
                    new_word |= word_offset(0, vertex);
                }
            }
        }
        else {
            uint64_t selected_count = 0;
            uint64_t child_indices[64];
            for (uint64_t i = 0; i < num_bits; i++) {
                uint64_t offset = toggle::pop_offset(i, word);
                uint64_t child_index = index + offset_index(layer, offset);
                T child_augval = tree[layer - 1][word_id(layer - 1, child_index)].augval;
                if (child_augval <= threshold) child_indices[selected_count++] = child_index;
                else {
                    aggregate = std::min(aggregate, child_augval);
                    new_word |= 1ULL << offset;
                }
            }
            T aggregates[64];
            uint64_t counts[64];
            toggle::parallel_for(0, selected_count, [&](uint64_t i) {
                counts[i] = extract_min(layer - 1, child_indices[i], threshold, f);
                aggregates[i] = tree[layer - 1][word_id(layer - 1, child_indices[i])].augval;
            }, 1);
            for (uint64_t i = 0; i < selected_count; i++) {
                count += counts[i];
                aggregate = std::min(aggregate, aggregates[i]);
                if (tree[layer - 1][word_id(layer - 1, child_indices[i])].flag) new_word |= word_offset(layer, child_indices[i]);
            }
        }
        node.flag = new_word;
        node.augval = aggregate;
        return count;
    }
    template<class F>
    uint64_t extract_min(T threshold, F&& f) noexcept {
        if (empty()) return 0;
        return extract_min(tree.size()-1, 0, threshold, f);
    }

    void extract_prefix_min(uint32_t layer, uint64_t index, T prefix_min) noexcept {
        NodeType& node = tree[layer][word_id(layer, index)];
        uint64_t word = node.flag;
        uint64_t new_word = 0;
        T aggregate = std::numeric_limits<T>::max();
        uint64_t num_bits = toggle::pop_count(word);
        if (layer == 0) {
            for (uint64_t i = 0; i < num_bits; i++) {
                uint64_t vertex = index + toggle::pop_offset(i, word);
                if (sequence[vertex] <= prefix_min) prefix_min = sequence[vertex];
                else {
                    new_word |= word_offset(0, vertex);
                    aggregate = std::min(aggregate, sequence[vertex]);
                }
            }
        }
        else {
            uint64_t selected_count = 0;
            uint64_t child_indices[64];
            T prefixes[64];
            for (uint64_t i = 0; i < num_bits; i++) {
                uint64_t offset = toggle::pop_offset(i, word);
                uint64_t child_index = index + offset_index(layer, offset);
                T child_augval = tree[layer - 1][word_id(layer - 1, child_index)].augval;
                if (child_augval <= prefix_min) {
                    child_indices[selected_count] = child_index;
                    prefixes[selected_count] = prefix_min;
                    prefix_min = child_augval;
                    selected_count++;
                }
                else {
                    new_word |= 1ULL << offset;
                    aggregate = std::min(aggregate, child_augval);
                }
            }
            toggle::parallel_for(0, selected_count, [&](uint64_t i) {
                extract_prefix_min(layer - 1, child_indices[i], prefixes[i]);
            }, 1);
            for (uint64_t i = 0; i < selected_count; i++) {
                NodeType& child = tree[layer - 1][word_id(layer - 1, child_indices[i])];
                if (child.flag) {
                    aggregate = std::min(aggregate, child.augval);
                    new_word |= word_offset(layer, child_indices[i]);
                }
            }
        }
        node.flag = new_word;
        node.augval = aggregate;
    }
    void extract_prefix_min() noexcept {
        if (empty()) return;
        extract_prefix_min(tree.size()-1, 0, std::numeric_limits<T>::max());
    }
};

} // namespace toggle
