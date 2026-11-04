// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "macros.h"

namespace toggle { namespace internal {

struct Job {
    std::atomic<bool> done;
    Job(): done(false) { }
    virtual void execute() = 0;
};

template <class F>
struct JobImpl : Job {
    F& f;
    JobImpl(F& _f) : f(_f) { }
    void execute() override { f(); done.store(true, std::memory_order_release); }
};

template <class ST = uint8_t, class DT = uint16_t, DT Capacity = 256>
struct alignas(4096) Deque {
    // Deque<uint8_t, uint16_t, 256>, Deque<uint32_t, uint64_t, 1024>
    static_assert(std::is_integral_v<ST> && std::is_unsigned_v<ST>);
    static_assert(std::is_integral_v<DT> && std::is_unsigned_v<DT>);
    static_assert(std::numeric_limits<DT>::digits == 2 * std::numeric_limits<ST>::digits);
    static_assert(Capacity >= 3 && Capacity <= (DT(1) << std::numeric_limits<ST>::digits));

    struct alignas(16) PJob { std::atomic<Job*> job; };
    alignas(16) std::array<PJob, Capacity> deq;
    alignas(16) std::atomic<DT> pos; 

    Deque(): pos(0) { for (PJob& x : deq) x.job.store(nullptr, std::memory_order_relaxed); }
    static constexpr int offset = std::numeric_limits<ST>::digits;
    static constexpr DT l = (DT(1) << std::numeric_limits<ST>::digits);
    static constexpr ST physic(ST logic) noexcept { if constexpr (Capacity == (DT(1) << std::numeric_limits<ST>::digits)) { return logic; } else { return logic % Capacity; }}
    static constexpr ST head(DT p) noexcept { return physic(ST(p)); }
    static constexpr ST tail(DT p) noexcept { return physic(ST(p >> offset)); }
    static constexpr ST length(DT p) noexcept { return physic(ST(p >> offset) - ST(p)); }

    std::pair<bool, bool> push_bottom(Job* job) noexcept {
        DT p = pos.load(std::memory_order_relaxed);
        if (length(p) == Capacity - 1) return {false, false};
        ST target = (length(p) == 0) ? tail(p) : tail(p - l);
        if (deq[target].job.load(std::memory_order_relaxed) != nullptr) { return {false, false}; }
        deq[target].job.store(job, std::memory_order_relaxed);
        #ifdef TOGGLE_TREE_WEAK_MEMORY
            pos.fetch_add((length(p) == 0) ? 2*l : l, std::memory_order_release);
        #else
            ST* tail_address = reinterpret_cast<ST*>(reinterpret_cast<unsigned char*>(&pos) + sizeof(ST));
            ST next_tail = ST(ST(p >> offset) + ((length(p) == 0) ? 2 : 1));
            if constexpr (sizeof(ST) == 1) {
                asm volatile("mov{b %[tail], (%[address]) | BYTE PTR [%[address]], %[tail]}" : : [tail] "q"(next_tail), [address] "r"(tail_address) : "memory");
            } 
            else if constexpr (sizeof(ST) == 2) {
                asm volatile("mov{w %[tail], (%[address]) | WORD PTR [%[address]], %[tail]}" : : [tail] "r"(next_tail), [address] "r"(tail_address) : "memory");
            } 
            else if constexpr (sizeof(ST) == 4) { 
                asm volatile("mov{l %[tail], (%[address]) | DWORD PTR [%[address]], %[tail]}": : [tail] "r"(next_tail), [address] "r"(tail_address) : "memory");
            } 
            else { static_assert(sizeof(ST) == 1 || sizeof(ST) == 2 || sizeof(ST) == 4); }
        #endif
        return {true, (length(p) <= 1)};
    }

    bool pop_bottom() noexcept {
        DT p = pos.load(std::memory_order_relaxed);
        if (length(p) <= 1) return false;
        p = pos.fetch_sub(l, std::memory_order_relaxed);
        if (length(p) == 1) return false;
        deq[tail(p - 2*l)].job.store(nullptr, std::memory_order_relaxed);
        return true;
    }

    std::pair<Job*, bool> pop_top() noexcept {
        DT p = pos.load(std::memory_order_relaxed);
        if (length(p) <= 1) return {nullptr, true};
        DT next = DT(((p >> offset) << offset) | ST(ST(p) + 1));
        if (!pos.compare_exchange_strong(p, next, std::memory_order_acquire, std::memory_order_relaxed)) return {nullptr, length(p) <= 1};
        Job* job = deq[head(p)].job.load(std::memory_order_relaxed);
        deq[head(p)].job.store(nullptr, std::memory_order_relaxed);
        return {job, length(p) == 2};
    }
};

struct Scheduler {
    using DQ = internal::Deque<uint8_t, uint16_t, 256>;
    std::vector<DQ> deques;
    std::vector<std::thread> spawned_threads;
    alignas (64) std::atomic<uint32_t> wait_flag;
    alignas (64) std::atomic<uint64_t> num_awake;
    alignas (64) std::atomic<uint64_t> num_exits;
    alignas (64) std::atomic<bool> finished_flag;
    alignas (64) uint64_t num_threads;
    inline static thread_local uint64_t thread_id = UINT64_MAX;
    inline static thread_local uint64_t dice = UINT64_MAX;

    uint64_t roll_dice() {
        dice ^= dice << 13; dice ^= dice >> 7; dice ^= dice << 17;
        uint64_t victim = dice % (num_threads - 1);
        return victim + (victim >= thread_id);
    };
    void go_sleep() {
        auto epoch = wait_flag.load(std::memory_order_relaxed);
        num_awake.fetch_sub(1, std::memory_order_release);
        std::atomic_wait_explicit(&wait_flag, epoch, std::memory_order_relaxed);
        num_awake.fetch_add(1, std::memory_order_relaxed);
    }
    void may_wake_others() {
        if (num_awake.load(std::memory_order_acquire) != num_threads) {
            wait_flag.fetch_add(1, std::memory_order_relaxed);
            std::atomic_notify_all(&wait_flag);
        };
    }

    Scheduler(): finished_flag(false) {
        auto env_p = std::getenv("TOGGLE_NUM_WORKERS");
        num_threads = env_p ? uint64_t(std::stoi(env_p)) : uint64_t(std::thread::hardware_concurrency());
        deques = std::vector<DQ>(num_threads);
        wait_flag = 0; num_awake = num_threads; num_exits = 0;
        thread_id = 0; dice = UINT64_MAX;
        for (uint64_t tid = 1; tid < num_threads; tid++) {
            spawned_threads.emplace_back([&, tid]() {
                thread_id = tid; dice = tid;
                go_sleep();
                auto start_time = std::chrono::steady_clock::now(); bool had_job = false;
                for (uint64_t i = 0; !finished_flag.load(std::memory_order_relaxed); i++) {
                    auto [job, empty] = deques[roll_dice()].pop_top();
                    if (!empty) { may_wake_others(); };
                    if (job != nullptr) { job->execute(); i = 0; had_job = true; }
                    if (i == num_threads * 200) {
                        i = 0;
                        auto now = std::chrono::steady_clock::now();
                        if (had_job) { start_time = now; had_job = false; }
                        if (now - start_time > std::chrono::microseconds{10000}) {
                            go_sleep();
                            start_time = std::chrono::steady_clock::now();
                        }
                        else std::this_thread::sleep_for(std::chrono::nanoseconds(num_threads * 200));
                    }
                }
                num_exits.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }
    ~Scheduler() {
        finished_flag.store(true, std::memory_order_relaxed);
        while (num_exits.load(std::memory_order_relaxed) != num_threads - 1) { may_wake_others(); }
        for (uint64_t i = 1; i < num_threads; i++) spawned_threads[i - 1].join();
    }

    void steal_while_waiting_for(std::atomic<bool>& flag) noexcept {
        for (uint64_t i = 0; !flag.load(std::memory_order_acquire); i++) {
            auto [job, empty] = deques[roll_dice()].pop_top();
            if (!empty) { may_wake_others(); };
            if (job != nullptr) { job->execute(); i = 0; }
            if (i == num_threads * 200) { std::this_thread::sleep_for(std::chrono::nanoseconds(num_threads * 200)); i = 0; }
        }
    }

    template <class LF, class RF>
    void parallel_do(LF&& lf, RF&& rf) noexcept {
        JobImpl<RF> right_job(rf);
        auto [in_deque, first] = deques[thread_id].push_bottom(&right_job);
        if (first) { may_wake_others(); };
        lf();
        if (!in_deque || deques[thread_id].pop_bottom()) { rf(); }
        else { steal_while_waiting_for(right_job.done); }
    }

    template <class F>
    void parallel_for(uint64_t start, uint64_t end, F&& f, uint64_t granularity) noexcept {
        if (end - start <= granularity) { for (uint64_t i = start; i < end; i++) { f(i); } return; }
        uint64_t mid = start + (end - start) / 2;
        parallel_do(
            [&]() { parallel_for(start, mid, f, granularity); },
            [&]() { parallel_for(mid, end, f, granularity); }
        );
    }
};

extern inline Scheduler& internal_get_scheduler() noexcept { static Scheduler scheduler; return scheduler; }
extern inline Scheduler& get_scheduler() noexcept { static thread_local Scheduler* const scheduler = &internal_get_scheduler(); return *scheduler; }

}} // namespace internal & toggle
