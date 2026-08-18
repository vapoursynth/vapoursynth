/*
* Copyright (c) 2012-2020 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef MEMORYUSE_H
#define MEMORYUSE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <random>

namespace vs {

// Memory allocation policy. Tracks all framebuffer allocations within a Core.
class MemoryUse {
    typedef std::multimap<size_t, uint8_t *> freelist_type;

    struct DebugStats;

    struct BlockHeader {
        size_t size;
    };
    static_assert(sizeof(BlockHeader) <= 16, "block header too large");

    std::mutex m_mutex;
    freelist_type m_freelist;
    std::minstd_rand m_prng;
    DebugStats *m_debug_stats = nullptr;

    std::atomic_size_t m_allocated{ 0 };
    std::atomic_size_t m_freelist_size{ 0 };
    std::atomic_size_t m_limit{ 0 };

    /* GPU memory is only accounted here, not allocated */
    std::atomic_size_t m_gpu_allocated{ 0 };
    std::atomic_size_t m_gpu_limit{ 0 };

    /* Also enforce a common limit when unified memory is used since it's allocated from the same underlying resource */
    std::atomic_bool m_unified{ false };
    std::atomic_size_t m_combined_limit{ 0 };
    std::atomic_size_t m_total_ram{ 0 };

    /* Teardown breadcrumb only; the "core is still here" guard it used to provide is now
       the unit m_live starts with, which no self delete can get past until on_core_freed
       gives it back. */
    std::atomic_bool m_core_freed{ false };

    /* One counter for everything that keeps this object reachable: a unit per outstanding
       byte in either pool, plus one unit held by the core itself and released in
       on_core_freed. Testing the pool counters separately cannot decide who tears the
       object down now that the host pool and the GPU pool drain on independent threads --
       each can read the other as already empty and both delete. Whoever decrements this to
       zero made the last access to the object and is the only one that deletes it. */
    std::atomic<uint64_t> m_live{ 1 };

    void live_acquire(uint64_t units) {
        m_live.fetch_add(units, std::memory_order_relaxed);
    }

    /* Must be the caller's final access to the object. */
    void live_release(uint64_t units) {
        if (units && m_live.fetch_sub(units, std::memory_order_acq_rel) == units)
            delete this;
    }

    static thread_local int64_t s_call_delta;
    static thread_local int64_t s_call_peak;
    static thread_local int64_t s_gpu_call_delta;
    static thread_local int64_t s_gpu_call_peak;

    static void track_allocated(size_t size) {
        s_call_delta += static_cast<int64_t>(size);
        if (s_call_delta > s_call_peak)
            s_call_peak = s_call_delta;
    }

    static void track_deallocated(size_t size) {
        s_call_delta -= static_cast<int64_t>(size);
    }

    static uint8_t *init_block(uint8_t *raw_ptr, size_t allocation_size);

    ~MemoryUse();

    void *do_allocate(size_t size);

    void do_deallocate(void *ptr);

    uint8_t *allocate_from_system(size_t size);

    uint8_t *allocate_from_freelist(size_t size);

    void deallocate_to_system(uint8_t *ptr, size_t size);

    void deallocate_to_freelist(uint8_t *ptr, size_t size);

    void gc_freelist();
public:
    MemoryUse();

    MemoryUse(const MemoryUse &) = delete;

    MemoryUse &operator=(const MemoryUse &) = delete;

    uint8_t *allocate(size_t size);

    void deallocate(uint8_t *buf);

    size_t set_limit(size_t bytes);

    size_t allocated_bytes() const { return m_allocated; }

    size_t limit() const { return m_limit; }

    bool is_over_limit() const { return m_allocated > m_limit || over_combined_limit(); }

    bool over_combined_limit() const {
        return m_unified && m_combined_limit && (m_allocated + m_gpu_allocated) > m_combined_limit;
    }

    bool is_under_limit() const {
        if (m_allocated >= (m_limit >> 1))
            return false;
        if (!m_unified || !m_combined_limit)
            return true;
        return (m_allocated + m_gpu_allocated) < (m_combined_limit >> 1);
    }

    void account_gpu(int64_t delta) {
        if (delta > 0)
            live_acquire(static_cast<uint64_t>(delta));
        m_gpu_allocated.fetch_add(static_cast<size_t>(delta), std::memory_order_relaxed);
        s_gpu_call_delta += delta;
        if (s_gpu_call_delta > s_gpu_call_peak)
            s_gpu_call_peak = s_gpu_call_delta;
        if (delta < 0)
            live_release(static_cast<uint64_t>(-delta));
    }

    size_t set_gpu_limit(size_t bytes) {
        m_gpu_limit = bytes;
        return m_gpu_limit;
    }

    size_t gpu_allocated_bytes() const { return m_gpu_allocated; }

    size_t gpu_limit() const { return m_gpu_limit; }

    bool is_gpu_over_limit() const { return m_gpu_allocated > m_gpu_limit || over_combined_limit(); }

    void set_unified(size_t combined_limit) {
        m_combined_limit = combined_limit;
        m_unified = true;
    }

    bool unified() const { return m_unified; }

    size_t combined_limit() const { return m_combined_limit; }

    /* What the retained freelist has to fit under. Normally the host limit, but on unified
       memory the GPU pool is the same RAM, so buffers kept back for reuse must give way as it
       grows. This is the only lever that returns host memory to the system -- eviction merely
       moves it here -- and on a unified device the only one of the two pools that gives
       anything back promptly: freeing GPU frames returns regions to the allocator's buckets,
       and their block stays committed until every region in it is free, so releasing 7 of 8
       held frames measured 0 of 415 MB returned. */
    size_t freelist_target() const {
        size_t limit = m_limit;
        if (m_unified && m_combined_limit) {
            size_t gpu = m_gpu_allocated;
            size_t room = m_combined_limit > gpu ? m_combined_limit - gpu : 0;
            if (room < limit)
                limit = room;
        }
        return limit;
    }

    size_t physical_memory() const { return m_total_ram; }

    struct CallTracking {
        int64_t delta;
        int64_t peak;
        int64_t gpu_delta;
        int64_t gpu_peak;
    };

    struct CallPeaks {
        int64_t host;
        int64_t gpu;
    };

    // Measures the peak net increase of allocated bytes caused by the calling thread between the
    // begin and end calls, used to predict how much memory a filter call will need. The state
    // returned by begin must be passed to the matching end call so nested measurements compose.
    static CallTracking begin_call_tracking() {
        CallTracking prev = { s_call_delta, s_call_peak, s_gpu_call_delta, s_gpu_call_peak };
        s_call_delta = 0;
        s_call_peak = 0;
        s_gpu_call_delta = 0;
        s_gpu_call_peak = 0;
        return prev;
    }

    static CallPeaks end_call_tracking(const CallTracking &prev) {
        int64_t peak = s_call_peak;
        s_call_peak = (prev.delta + peak > prev.peak) ? (prev.delta + peak) : prev.peak;
        s_call_delta += prev.delta;
        int64_t gpu_peak = s_gpu_call_peak;
        s_gpu_call_peak = (prev.gpu_delta + gpu_peak > prev.gpu_peak) ? (prev.gpu_delta + gpu_peak) : prev.gpu_peak;
        s_gpu_call_delta += prev.gpu_delta;
        return { peak, gpu_peak };
    }

    // Called only from VSCore destructor.
    void on_core_freed();
};

} // namespace vs

#endif // MEMORYUSE_H
