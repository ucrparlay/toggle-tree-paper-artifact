// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#if defined(TOGGLE_TREE)
    #include "../parallel/parallel.h"
#endif

namespace toggle {

struct alignas(64) Timer {
    using clock_t = std::chrono::steady_clock;

    clock_t::time_point last;
    std::vector<clock_t::duration> record;

    Timer(uint64_t n = 1) : last(clock_t::now()), record(n, clock_t::duration::zero()) {}

    void start() {
        last = clock_t::now();
    }

    double stop(uint64_t i = 0) {
        auto tt = clock_t::now() - last;
        record[i] += tt;
        return std::chrono::duration<double>(tt).count();
    }

    clock_t::duration duration(uint64_t i = 0) {
        return record[i];
    }

    void clear() {
        for (uint64_t i = 0; i < record.size(); i++) {
            record[i] = clock_t::duration::zero();
        }
    }

    void show() {
        for (uint64_t i = 0; i < record.size(); i++) {
            double seconds = std::chrono::duration<double>(record[i]).count();
            std::cout << "t" << i << " = " << seconds << " sec\n";
        }
    }
};

#if defined(TOGGLE_TREE) || defined(PARLAY_PARALLEL_H_)
    #if defined(TOGGLE_TREE)
        namespace timer_namespace = toggle;
    #else
        namespace timer_namespace = parlay;
    #endif

struct ThreadTimer {
    using clock_t = std::chrono::steady_clock;

    std::vector<Timer> timers;
    ThreadTimer(uint64_t n = 1) { timers = std::vector<Timer>(timer_namespace::num_workers(), Timer(n)); }

    void start() { timers[timer_namespace::worker_id()].start(); }

    double stop(uint64_t i = 0) { return timers[timer_namespace::worker_id()].stop(i); }

    double duration(uint64_t i = 0) {
        auto sum_duration = clock_t::duration::zero();
        for (uint64_t tid = 0; tid < timer_namespace::num_workers(); tid++) { sum_duration += timers[tid].duration(i); }
        return std::chrono::duration<double>(sum_duration).count() / double(timer_namespace::num_workers());
    }

    void show() {
        for (uint64_t i=0; i<timers[0].record.size(); i++) {
            std::cout << "t" << i << " = " << duration(i) << "\n";
        }
    }
};

struct ThreadCounter {
    struct alignas(64) Node {
        uint64_t data[8] = {0};
    };
    std::vector<Node> counter;
    ThreadCounter() { counter = std::vector<Node>(timer_namespace::num_workers(), Node()); }

    void add(uint64_t i, uint64_t amount) { counter[timer_namespace::worker_id()].data[i] += amount; }

    uint64_t current(uint64_t i) {
        uint64_t sum_count = 0;
        for (uint64_t tid = 0; tid < timer_namespace::num_workers(); tid++) { sum_count += counter[tid].data[i]; }
        return sum_count;
    }

    void show() {
        std::cout << "thread_local_counter = ";
        for (uint64_t i=0; i<7; i++) {
            std::cout << current(i) << ", ";
        }
        std::cout << current(7) << "\n";
    }
};

#endif

} // namespace toggle
