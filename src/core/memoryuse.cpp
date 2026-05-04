#include <cassert>
#include <climits>
#include <cstdint>
#include <mutex>
#include <random>
#include "memoryuse.h"
#include "VSHelper4.h"

// Only confirmed needed on Windows.
#ifdef _WIN32
#define USE_FRAME_POOL
#endif

// Print pool hit/miss stats on exit.
#if 0
#define DEBUG_STATS
#endif

namespace vs {

namespace {

#ifdef USE_FRAME_POOL
constexpr size_t SYSTEM_ALLOCATOR_THRESHOLD = 1UL << 20;
#else
constexpr size_t SYSTEM_ALLOCATOR_THRESHOLD = SIZE_MAX;
#endif
constexpr size_t ALIGNMENT = 64;
constexpr size_t GOOD_FIT_NUMERATOR = 1;
constexpr size_t GOOD_FIT_DENOMINATOR = 8;

bool is_good_fit(size_t request, size_t allocated)
{
    assert(request <= allocated);
    size_t wasted = allocated - request;
    return wasted <= (request / GOOD_FIT_DENOMINATOR) * GOOD_FIT_NUMERATOR;
}

} // namespace

#ifdef DEBUG_STATS
struct MemoryUse::DebugStats {
    std::atomic_size_t gc_count{ 0 };
    std::atomic_size_t small_malloc_count{ 0 };
    std::atomic_size_t small_free_count{ 0 };
    std::atomic_size_t large_malloc_count{ 0 };
    std::atomic_size_t large_free_count{ 0 };
};
#endif

MemoryUse::MemoryUse()
{
#if SIZE_MAX > UINT32_MAX
    m_limit = 4 * (1ULL << 30);
#else
    m_limit = 1 * (1ULL << 30);
#endif

#ifdef DEBUG_STATS
    m_debug_stats = new DebugStats{};
#endif
}

MemoryUse::~MemoryUse()
{
    assert(!m_allocated);

#ifdef DEBUG_STATS
    size_t num_keys = 0;
    size_t size_class = 0;
#endif

    for (auto &entry : m_freelist) {
#ifdef DEBUG_STATS
        if (entry.first != size_class) {
            size_class = entry.first;
            ++num_keys;
        }
#endif

        do_deallocate(entry.second);
    }

#ifdef DEBUG_STATS
    fprintf(stderr, "Small Allocations: %zu\n", m_debug_stats->small_malloc_count.load());
    fprintf(stderr, "Large Allocations: %zu\n", m_debug_stats->large_malloc_count.load());
    fprintf(stderr, "Small Deallocations: %zu\n", m_debug_stats->small_free_count.load());
    fprintf(stderr, "Large Deallocations: %zu\n", m_debug_stats->large_free_count.load());
    fprintf(stderr, "GC Deallocations: %zu\n", m_debug_stats->gc_count.load());
    fprintf(stderr, "Freelist Length: %zu\n", m_freelist.size());
    fprintf(stderr, "Freelist Size Classes: %zu\n", num_keys);
    delete m_debug_stats;
#endif
}

uint8_t *MemoryUse::init_block(uint8_t *raw_ptr, size_t allocation_size)
{
    BlockHeader *header = new (raw_ptr) BlockHeader{};
    header->size = allocation_size;
    return raw_ptr + ALIGNMENT;
}

void *MemoryUse::do_allocate(size_t size)
{
    return vsh::vsh_aligned_malloc(size, ALIGNMENT);
}

void MemoryUse::do_deallocate(void *ptr)
{
    vsh::vsh_aligned_free(ptr);
}

uint8_t *MemoryUse::allocate_from_system(size_t size)
{
#ifdef DEBUG_STATS
    if (size > SYSTEM_ALLOCATOR_THRESHOLD)
        ++m_debug_stats->large_malloc_count;
    else
        ++m_debug_stats->small_malloc_count;
#endif

    uint8_t *raw_ptr = static_cast<uint8_t *>(do_allocate(size));
    if (!raw_ptr)
        return nullptr;

    uint8_t *user_ptr = init_block(raw_ptr, size);
    m_allocated += size;
    return user_ptr;
}

uint8_t *MemoryUse::allocate_from_freelist(size_t size)
{
    std::lock_guard<std::mutex> lock{ m_mutex };

    auto iter = m_freelist.lower_bound(size);
    if (iter != m_freelist.end() && is_good_fit(size, iter->first)) {
        assert(m_freelist_size >= iter->first);

        uint8_t *raw_ptr = iter->second;
        size_t block_size = iter->first;

        m_freelist.erase(iter);
        m_freelist_size -= block_size;
        m_allocated += block_size;

        return raw_ptr + ALIGNMENT;
    }
    return nullptr;
}

void MemoryUse::deallocate_to_system(uint8_t *ptr, size_t size)
{
#ifdef DEBUG_STATS
    if (size > SYSTEM_ALLOCATOR_THRESHOLD)
        ++m_debug_stats->large_free_count;
    else
        ++m_debug_stats->small_free_count;
#endif

    do_deallocate(ptr);
    m_allocated -= size;
}

void MemoryUse::deallocate_to_freelist(uint8_t *ptr, size_t size)
{
    std::lock_guard<std::mutex> lock{ m_mutex };
    m_freelist.emplace(size, ptr);
    m_freelist_size += size;
    m_allocated -= size;
}

void MemoryUse::gc_freelist()
{
    size_t total = m_allocated + m_freelist_size;
    size_t limit = m_limit;

    while (total > limit) {
        std::unique_lock<std::mutex> lock{ m_mutex };

        // Freelist is already empty. All remaining memory is working memory.
        if (m_freelist.empty())
            return;

        // Recalculate while holding the mutex.
        total = m_allocated + m_freelist_size;
        limit = m_limit;

        if (total <= limit)
            return;

        // Pick a random buffer to minimize the risk of thrashing.
        std::uniform_int_distribution<size_t> dist(0, m_freelist.size() - 1);
        size_t index = dist(m_prng);

        auto iter = m_freelist.begin();
        std::advance(iter, index);

        size_t size = iter->first;
        uint8_t *ptr = iter->second;
        assert(size == reinterpret_cast<BlockHeader *>(ptr)->size);
        assert(size <= m_freelist_size);

        m_freelist.erase(iter);
        m_freelist_size -= size;

        lock.unlock();

        // Buffer was on the freelist. Do not change allocated bytes counter.
        do_deallocate(ptr);
        total -= size;

#ifdef DEBUG_STATS
        ++m_debug_stats->gc_count;
#endif
    }
}

uint8_t *MemoryUse::allocate(size_t size)
{
    assert(size < SIZE_MAX - 4095 - 64);

    size_t aligned_size = (size + ALIGNMENT + (ALIGNMENT - 1)) & ~static_cast<size_t>(ALIGNMENT - 1);
    size_t page_aligned_size = (aligned_size + 4095) & ~static_cast<size_t>(4095);

    if (page_aligned_size <= SYSTEM_ALLOCATOR_THRESHOLD)
        return allocate_from_system(aligned_size); // Don't align small buffers to 4k.
    else if (uint8_t *cached = allocate_from_freelist(page_aligned_size))
        return cached;
    else
        return allocate_from_system(page_aligned_size);
}

void MemoryUse::deallocate(uint8_t *buf)
{
    assert(buf);

    uint8_t *raw_ptr = buf - ALIGNMENT;
    BlockHeader *header = reinterpret_cast<BlockHeader *>(raw_ptr);
    assert(header->size);

    size_t size = header->size;
    bool to_freelist = size > SYSTEM_ALLOCATOR_THRESHOLD;

    if (to_freelist)
        deallocate_to_freelist(raw_ptr, size);
    else
        deallocate_to_system(raw_ptr, size);

    if (m_core_freed && m_allocated == 0) {
        delete this;
        return;
    }

    if (to_freelist)
        gc_freelist();
}

size_t MemoryUse::set_limit(size_t bytes)
{
    m_limit = bytes;
    gc_freelist();
    return m_limit;
}

void MemoryUse::on_core_freed()
{
    bool was_idle = m_allocated == 0;

    m_core_freed = true;

    // Only the core can create a new allocation from the zero-memory state.
    if (was_idle) {
        assert(!m_allocated);
        delete this;
    }
}

} // namespace vs
