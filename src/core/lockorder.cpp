#include "lockorder.h"

#ifdef VS_LOCK_ORDER_CHECK

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

static const char *const lockNames[vsLockCount] = {
    "vulkanDeviceLock", "execPoolsMutex", "claimMutex", "queueLock",
    "flushMutex", "allocatorMutex", "cacheLock", "cacheMutex", "logMutex"
};

const char *vsLockName(int id) {
    return (id >= 0 && id < vsLockCount) ? lockNames[id] : "?";
}

namespace {
/* Its own lock, deliberately not one of the tracked ones. */
/* Namespace scope, not function-local statics: a function-local static and std::call_once both
   use a guard variable, and a child forked while another thread is inside one inherits it stuck
   in progress and deadlocks on the next call. environment_test forks, which is exactly how this
   was found. These are constructed before main, so no guard is ever consulted at runtime. */
std::mutex edgesMutexObj;
std::set<std::pair<int, int>> edgesObj;
std::set<std::string> boundaryViolationsObj;
std::mutex &edgesMutex() { return edgesMutexObj; }
std::set<std::pair<int, int>> &edges() { return edgesObj; }
std::set<std::string> &boundaryViolations() { return boundaryViolationsObj; }

thread_local std::vector<int> held;

void report() {
    std::lock_guard<std::mutex> lock(edgesMutex());
    std::printf("\n=== lock order: %zu edge(s) observed ===\n", edges().size());
    for (const auto &e : edges())
        std::printf("  %-18s -> %s\n", vsLockName(e.first), vsLockName(e.second));

    int inversions = 0;
    for (const auto &e : edges()) {
        if (e.first < e.second && edges().count({e.second, e.first})) {
            std::printf("  INVERSION: %s and %s were both taken under each other\n",
                vsLockName(e.first), vsLockName(e.second));
            inversions++;
        }
    }
    for (const auto &b : boundaryViolations())
        std::printf("  BOUNDARY VIOLATION: %s\n", b.c_str());

    if (!inversions && boundaryViolations().empty())
        std::printf("no inversion, nothing held across a boundary\n");
    else
        std::printf("%d inversion(s), %zu boundary violation(s)\n",
            inversions, boundaryViolations().size());
}
}

/* Fast path. The first version took the global mutex and did a set insert on every acquisition,
   which serialised the whole core through one lock -- cacheMutex alone is taken per frame per
   node, and the suite went from seconds to not finishing. An edge only ever needs recording the
   first time it occurs, so check a lock-free table and touch the mutex only for a new one. */
static std::atomic<bool> seenEdge[vsLockCount][vsLockCount];
/* Registered before main for the same fork reason: no call_once, no lazy guard. */
static const bool reportRegistered = [] { std::atexit(&report); return true; }();

void vsLockOrderPush(int id) {
    (void)reportRegistered;
    for (int outer : held) {
        if (seenEdge[outer][id].load(std::memory_order_relaxed))
            continue;
        seenEdge[outer][id].store(true, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(edgesMutex());
        edges().insert({outer, id});
    }
    held.push_back(id);
}

void vsLockOrderPop(int id) {
    for (size_t i = held.size(); i-- > 0;) {
        if (held[i] == id) {
            held.erase(held.begin() + i);
            return;
        }
    }
}

void vsLockOrderAssertNoneHeld(const char *boundary) {
    if (held.empty())
        return;
    std::string msg = std::string(boundary) + " reached holding:";
    for (int id : held)
        msg += std::string(" ") + vsLockName(id);
    std::lock_guard<std::mutex> lock(edgesMutex());
    boundaryViolations().insert(msg);
}

#endif /* VS_LOCK_ORDER_CHECK */
