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

    std::atomic_bool m_core_freed{ false };

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

    // Called only from VSCore destructor.
    void on_core_freed();
};

} // namespace vs

#endif // MEMORYUSE_H
