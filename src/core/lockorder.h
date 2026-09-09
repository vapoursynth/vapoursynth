/* Lock-order instrumentation for the graph in section 2 of vsvulkanexec_protocol.md.
 *
 * That section says the graph is "measured rather than argued": every lock acquisition was
 * instrumented to record what the acquiring thread already held, collected over the suites and
 * the GPU workloads, and no pair appeared in both orders. The instrumentation itself was not
 * kept. This is it rebuilt, to the description given there -- a scoped object recording the
 * thread's held set after each acquisition, plus a check at each of the four boundaries where
 * nothing of ours may be held.
 *
 * Why this rather than ThreadSanitizer, which already reports lock-order inversions: TSan can
 * only report a pair it has seen taken in BOTH orders, so it needs the bad interleaving to
 * actually happen. This records every edge the first time it occurs, so a newly added edge shows
 * up as a new line in the report whether or not anything ever took it the other way. The two are
 * complementary and the cost here is a thread_local push and pop.
 *
 * Off unless VS_LOCK_ORDER_CHECK is defined, and compiles to nothing when it is not.
 */
#ifndef VS_LOCKORDER_H
#define VS_LOCKORDER_H

enum VSLockId {
    vsLockVulkanDevice = 0, /* VSCore::vulkanDeviceLock */
    vsLockExecPools,        /* VSVulkanDevice::execPoolsMutex */
    vsLockClaim,            /* VSVulkanExecPool::claimMutex */
    vsLockQueue,            /* VSVulkanQueue, the one a plugin can hold */
    vsLockFlush,            /* VSVulkanDevice::flushMutex */
    vsLockAllocator,        /* the allocator's block and free lists */
    vsLockCacheSet,         /* VSCore::cacheLock */
    vsLockNodeCache,        /* VSNode::cacheMutex */
    vsLockLog,              /* VSCore::logMutex */
    vsLockCount
};

#ifdef VS_LOCK_ORDER_CHECK

const char *vsLockName(int id);
/* Records (everything already held) -> id, and pops on scope exit. */
void vsLockOrderPush(int id);
void vsLockOrderPop(int id);
/* The four boundaries where the core must hold nothing of its own: a filter's getFrame, a
   release callback, a filter free callback and a plugin function's invoke. */
void vsLockOrderAssertNoneHeld(const char *boundary);

namespace vs {
class LockHeld {
public:
    explicit LockHeld(int id) : id_(id) { vsLockOrderPush(id_); }
    ~LockHeld() { vsLockOrderPop(id_); }
    LockHeld(const LockHeld &) = delete;
    LockHeld &operator=(const LockHeld &) = delete;
private:
    int id_;
};
}

#define VS_LOCK_PASTE2(a, b) a##b
#define VS_LOCK_PASTE(a, b) VS_LOCK_PASTE2(a, b)
#define VS_LOCK_HELD(id) ::vs::LockHeld VS_LOCK_PASTE(vsLockHeld_, __LINE__)(id)
#define VS_LOCK_BOUNDARY(what) ::vsLockOrderAssertNoneHeld(what)

#else

#define VS_LOCK_HELD(id) ((void)0)
#define VS_LOCK_BOUNDARY(what) ((void)0)

#endif /* VS_LOCK_ORDER_CHECK */

#endif /* VS_LOCKORDER_H */
