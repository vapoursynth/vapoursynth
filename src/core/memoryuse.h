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

    /* GPU memory is only accounted here, never allocated: the Vulkan block allocator reports
       block grants and returns through account_gpu so cache pressure can see both pools in
       one place, which is the whole reason this lives in MemoryUse instead of its own class.
       Blocks, not the regions carved out of them, because a block is what the driver's budget
       is actually spent on -- accounting the live subset let the core sit inside its limit
       while the driver held nearly twenty percent more and ran into the wall. */
    std::atomic_size_t m_gpu_allocated{ 0 };
    std::atomic_size_t m_gpu_limit{ 0 };

    std::atomic_bool m_core_freed{ false };

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

    bool is_over_limit() const { return m_allocated > m_limit; }

    bool is_under_limit() const { return m_allocated < (m_limit >> 1); }

    void account_gpu(int64_t delta) {
        m_gpu_allocated.fetch_add(static_cast<size_t>(delta), std::memory_order_relaxed);
        /* The same per call tracking the host side does, so the thread pool can predict GPU
           allocations of filter calls too. Frees landing on other threads dent their deltas
           harmlessly, exactly as host side frees already do. */
        s_gpu_call_delta += delta;
        if (s_gpu_call_delta > s_gpu_call_peak)
            s_gpu_call_peak = s_gpu_call_delta;
        /* Mirrors the self delete in do_deallocate. GPU frames outliving the core keep the
           device alive, and the last of them to go takes the device with it, which tears the
           blocks down through here — so reaching zero means the allocator has nothing left to
           report and cannot call back into a deleted this. The plain reads are enough for the
           same reason as the host side: only the core creates allocations, and surviving
           references die one at a time. */
        if (delta < 0 && m_core_freed && !m_allocated && !m_gpu_allocated)
            delete this;
    }

    size_t set_gpu_limit(size_t bytes) {
        m_gpu_limit = bytes;
        return m_gpu_limit;
    }

    size_t gpu_allocated_bytes() const { return m_gpu_allocated; }

    size_t gpu_limit() const { return m_gpu_limit; }

    bool is_gpu_over_limit() const { return m_gpu_allocated > m_gpu_limit; }

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
