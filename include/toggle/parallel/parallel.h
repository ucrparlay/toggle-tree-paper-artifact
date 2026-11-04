// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#if defined(TOGGLE_TREE_USE_PARLAY)
    #include <parlay/parallel.h>
#else
    #include "scheduler.h"
#endif

namespace toggle {

#if defined(TOGGLE_TREE_USE_PARLAY)
    using parlay::num_workers;
    using parlay::worker_id;
    using parlay::parallel_for;
    using parlay::parallel_do;
#else
    inline uint64_t num_workers() noexcept { return internal::get_scheduler().num_threads; }
    inline uint64_t worker_id() noexcept { return internal::Scheduler::thread_id; }
    template <class F>
    void parallel_for(uint64_t start, uint64_t end, F&& f, uint64_t granularity) noexcept {
        internal::get_scheduler().parallel_for(start, end, f, granularity);
    }
    template <class LF, class RF>
    void parallel_do(LF&& lf, RF&& rf) noexcept {
        internal::get_scheduler().parallel_do(lf, rf);
    }
#endif

} // namespace toggle
