// SPDX-License-Identifier: MIT
#pragma once

#define TOGGLE_TREE

#ifndef TOGGLE_TREE_WEAK_MEMORY
    #if !defined(__x86_64__) || !(defined(__GNUC__) || defined(__clang__)) || !defined(__BYTE_ORDER__) || !(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        #define TOGGLE_TREE_WEAK_MEMORY
    #endif
#endif
