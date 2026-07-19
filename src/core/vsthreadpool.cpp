/*
* Copyright (c) 2012-2021 Fredrik Mellbin
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

#include "vscore.h"
#include <cassert>
#include <bitset>
#include <chrono>
#ifdef VS_TARGET_CPU_X86
#include "x86utils.h"
#endif

#if defined(HAVE_SCHED_GETAFFINITY)
#include <sched.h>
#elif defined(HAVE_CPUSET_GETAFFINITY)
#include <sys/param.h>
#include <sys/_cpuset.h>
#include <sys/cpuset.h>
#endif

// how often caches may adjust their size based on recent hit/miss statistics, wall clock based
// so the rate doesn't depend on how many internal requests a script generates per output frame,
// memory pressure only shortens the interval instead of forcing a sweep on every completed task
static constexpr int64_t normalCacheSweepInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(500)).count();
static constexpr int64_t pressureCacheSweepInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(150)).count();

// pacing for the AIMD style thread ceiling controller, over limit usage must persist for the
// confirm interval before the ceiling is halved since brief spikes from single allocation heavy
// filter calls resolve on their own, after that the ceiling steps down while usage stays over
// the limit and a pipeline flush only happens as a last resort when a single running thread
// still isn't enough, regrowth is slower above the level where memory last ran out
static constexpr int64_t episodeConfirmInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(100)).count();
static constexpr int64_t ceilingShrinkInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count();
static constexpr int64_t flushAtFloorInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(1000)).count();
static constexpr int64_t ceilingGrowIntervalFast = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(1000)).count();
static constexpr int64_t ceilingGrowIntervalSlow = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(4000)).count();

static int64_t steadyClockNow() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

size_t VSThreadPool::getNumAvailableThreads() {
    size_t nthreads = std::thread::hardware_concurrency();
#ifdef _WIN32
    DWORD_PTR pAff = 0;
    DWORD_PTR sAff = 0;
    BOOL res = GetProcessAffinityMask(GetCurrentProcess(), &pAff, &sAff);
    if (res && pAff != 0) {
        std::bitset<sizeof(sAff) * 8> b(pAff);
        nthreads = b.count();
    }
#elif defined(HAVE_SCHED_GETAFFINITY)
    // Linux only.
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(cpu_set_t), &affinity) == 0)
        nthreads = CPU_COUNT(&affinity);
#elif defined(HAVE_CPUSET_GETAFFINITY)
    // BSD only (FreeBSD only?)
    cpuset_t affinity;
    if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(cpuset_t), &affinity) == 0)
        nthreads = CPU_COUNT(&affinity);
#endif

    return nthreads;
}

bool VSThreadPool::taskCmp(const PVSFrameContext &a, const PVSFrameContext &b) {
    return (a->reqOrder < b->reqOrder) || (a->reqOrder == b->reqOrder && a->key.second < b->key.second);
}

void VSThreadPool::runTasksWrapper(VSThreadPool *owner, bool &stop) {
    owner->runTasks(stop);
}

void VSThreadPool::runTasks(bool &stop) {
#ifdef VS_TARGET_CPU_X86
    if (!vs_isSSEStateOk())
        core->logFatal("Bad SSE state detected after creating new thread");
#endif

    std::unique_lock<std::mutex> lock(taskLock);

    std::string deferredLog;

    while (true) {
        // status messages are built while holding taskLock but only emitted here after releasing it,
        // log handlers can block on locks whose holders in turn take taskLock (the python bindings
        // acquire the GIL in their handler) so dispatching them under the lock can deadlock
        if (!deferredLog.empty()) {
            lock.unlock();
            core->logMessage(mtInformation, deferredLog);
            deferredLog.clear();
            lock.lock();
        }

        bool ranTask = false;

/////////////////////////////////////////////////////////////////////////////////////////////
// Go through all tasks from the top (oldest) and process the first one possible

        // Note that seenNodes is only used for fast early rejection, reaching the size limit will not cause any correctness issues
        // Even with many threads and complicated scripts the full task queue is generally less than 10 items
        constexpr size_t maxSeenNodes = 64;
        VSNode *seenNodes[maxSeenNodes];
        size_t seenCount = 0;

        for (auto iter = tasks.begin(); iter != tasks.end(); ++iter) {
            VSFrameContext *frameContext = iter->get();
            VSNode *node = frameContext->key.first;

/////////////////////////////////////////////////////////////////////////////////////////////
// Fast path if a frame is cached

            if (node->cacheEnabled && frameContext->first) {
                PVSFrame f = node->getCachedFrameInternal(frameContext->key.second);

                if (f) {
                    bool needsSort = false;

                    for (size_t i = 0; i < frameContext->notifyCtxList.size(); i++) {
                        PVSFrameContext &notify = frameContext->notifyCtxList[i];
                        notify->availableFrames.push_back({frameContext->key, f});

                        assert(notify->numFrameRequests > 0);
                        if (--notify->numFrameRequests == 0) {
                            queueTask(notify);
                            needsSort = true;
                        }
                    }

                    // this is needed in order to prevent more tasks latching on to a context in the final stages of completion, holds a reference to frameContext
                    PVSFrameContext mainContextRef = std::move(*iter);  
                    tasks.erase(iter);

                    if (frameContext->external)
                        returnFrame(frameContext, f, lock);
                    else
                        allContexts.erase(frameContext->key);

                    if (needsSort)
                        tasks.sort(taskCmp);

                    ranTask = true;
                    break;
                }
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// This part handles the locking for the different filter modes

            int filterMode = node->filterMode;

            // Don't try to lock the same node twice since it's likely to fail and will produce more out of order requests as well
            if (filterMode != fmFrameState) {
                bool alreadySeen = false;
                for (size_t i = 0; i < seenCount; i++) {
                    if (seenNodes[i] == node) {
                        alreadySeen = true;
                        break;
                    }
                }
                if (alreadySeen)
                    continue;
                if (seenCount < maxSeenNodes)
                    seenNodes[seenCount++] = node;
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Admission control, some filters allocate several frames worth of temporary memory in a
// single call and having many threads enter such calls at once can overshoot the memory limit
// by a large factor before any other mechanism gets a chance to react, so don't start calls
// whose predicted allocations don't fit in the allowed overshoot, progress is guaranteed
// since tasks are always admitted when no other filter call is running

            int64_t expectedAlloc = node->expectedTransientAllocation();
            if (expectedAlloc > 0 && processingThreads.load(std::memory_order_relaxed) > 0) {
                int64_t memLimit = static_cast<int64_t>(core->memory->limit());
                if (static_cast<int64_t>(core->memory->allocated_bytes()) + inflightAllocation.load(std::memory_order_relaxed) + expectedAlloc > memLimit + memLimit / 4)
                    continue;
            }

            // Does the filter need the per instance mutex? fmFrameState, fmUnordered and fmParallelRequests (when in the arAllFramesReady state) use this
            bool useSerialLock = (filterMode == fmFrameState || filterMode == fmUnordered || (filterMode == fmParallelRequests && !frameContext->first));

            if (useSerialLock) {
                if (!node->serialMutex.try_lock())
                    continue;
                if (filterMode == fmFrameState) {
                    if (node->serialFrame == -1) {
                        node->serialFrame = frameContext->key.second;
                        node->serialOwner = frameContext;
                    } else if (node->serialOwner != frameContext) {
                        node->serialMutex.unlock();
                        continue;
                    }
                }
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Remove the context from the task list and keep references around until processing is done

            PVSFrameContext frameContextRef = std::move(*iter);
            tasks.erase(iter);

/////////////////////////////////////////////////////////////////////////////////////////////
// Figure out the activation reason

            assert(frameContext->numFrameRequests == 0);
            int ar = arInitial;
            if (frameContext->hasError()) {
                ar = arError;
            } else if (!frameContext->first) {
                ar = (node->apiMajor == 3) ? static_cast<int>(vs3::arAllFramesReady) : static_cast<int>(arAllFramesReady);
            } else {
                frameContext->first = false;
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Do the actual processing

            processingThreads.fetch_add(1, std::memory_order_relaxed);
            inflightAllocation.fetch_add(expectedAlloc, std::memory_order_relaxed);

            lock.unlock();

            PVSFrame f = node->getFrameInternal(frameContext->key.second, ar, frameContext);
            ranTask = true;

            inflightAllocation.fetch_sub(expectedAlloc, std::memory_order_relaxed);
            processingThreads.fetch_sub(1, std::memory_order_relaxed);

            bool frameProcessingDone = f || frameContext->hasError();
            if (frameContext->hasError() && f)
                core->logFatal("A frame was returned by " + node->name + " but an error was also set, this is not allowed");

/////////////////////////////////////////////////////////////////////////////////////////////
// Unlock so the next job can run on the context
            if (useSerialLock) {
                if (frameProcessingDone && filterMode == fmFrameState) {
                    node->serialFrame = -1;
                    node->serialOwner = nullptr;
                }
                node->serialMutex.unlock();
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Handle frames that were requested
            bool requestedFrames = frameContext->reqList.size() > 0 && !frameProcessingDone;
            if (f && requestedFrames)
                core->logFatal("A frame was returned at the end of processing by " + node->name + " but there are still outstanding requests");

            bool needsSort = requestedFrames;

            // memory pressure is sampled after every completed call and not only after request
            // issuing ones, request cascades mostly happen up front so gating on them leaves long
            // stretches of pure frame production completely unmanaged exactly when usage climbs
            // the fastest, the wall clock pacing keeps the cost of this at a single clock read
            bool overLimit = core->memory->is_over_limit();
            int64_t timeNow = steadyClockNow();
            if (timeNow - lastCacheSweep.load(std::memory_order_relaxed) >= (overLimit ? pressureCacheSweepInterval : normalCacheSweepInterval)) {
                if (!cacheSweepActive.exchange(true)) {
                    lastCacheSweep = timeNow;
                    core->notifyCaches(overLimit);
                    cacheSweepActive = false;
                }
            }

            lock.lock();

            if (expectedAlloc > 0 && idleThreads > 0)
                newWork.notify_one(); // tasks skipped by admission control may fit now

            if (requestedFrames) {
                assert(frameContext->numFrameRequests == 0);

                for (size_t i = 0; i < frameContext->reqList.size(); i++)
                    startInternalRequest(frameContextRef, frameContext->reqList[i]);

                frameContext->numFrameRequests = frameContext->reqList.size();
                frameContext->reqList.clear();
            }

            if (overLimit) {
                if (!inPressureEpisode) {
                    if (!overLimitSince) {
                        overLimitSince = timeNow;
                    } else if (timeNow - overLimitSince >= episodeConfirmInterval) {
                        // sustained overrun and not just a brief spike from a single allocation
                        // heavy filter call, halve the ceiling and continue stepping from there
                        inPressureEpisode = true;
                        threadsThresh = std::max<size_t>(currentMaxThreads / 2, 1);
                        if (currentMaxThreads != threadsThresh) {
                            currentMaxThreads = threadsThresh;
                            deferredLog = "Maximum running threads reduced to " + std::to_string(currentMaxThreads) + "/" + std::to_string(maxThreads) + " due to excessive memory usage";
                        }
                        lastShrink = timeNow;
                    }
                } else if (timeNow - lastShrink >= ceilingShrinkInterval) {
                    if (currentMaxThreads > 1) {
                        --currentMaxThreads;
                        lastShrink = timeNow;
                        deferredLog = "Maximum running threads reduced to " + std::to_string(currentMaxThreads) + "/" + std::to_string(maxThreads) + " due to excessive memory usage";
                    } else if (!flushCaches && timeNow - lastShrink >= flushAtFloorInterval) {
                        // reducing concurrency didn't help so the memory must be held by caches
                        // or by allocations the core can't see, flush as a last resort
                        flushCaches = true;
                        deferredLog = "Memory usage still over the limit with a single running thread, flushing pipeline";
                    }
                }
            } else {
                overLimitSince = 0;
                if (core->memory->is_under_limit()) {
                    inPressureEpisode = false;
                    if (currentMaxThreads < maxThreads) {
                        // probe more carefully once past the level where memory last ran out
                        int64_t interval = (currentMaxThreads < threadsThresh) ? ceilingGrowIntervalFast : ceilingGrowIntervalSlow;
                        if (timeNow - lastGrow >= interval) {
                            ++currentMaxThreads;
                            lastGrow = timeNow;
                            deferredLog = "Maximum running threads increased to " + std::to_string(currentMaxThreads) + "/" + std::to_string(maxThreads) + " due to more memory being available";
                        }
                    }
                }
            }

            if (frameProcessingDone && !frameContext->external)
                allContexts.erase(frameContext->key);

/////////////////////////////////////////////////////////////////////////////////////////////
// Notify all dependent contexts

            if (frameContext->hasError()) {
                for (size_t i = 0; i < frameContextRef->notifyCtxList.size(); i++) {
                    PVSFrameContext &notify = frameContextRef->notifyCtxList[i];
                    notify->setError(frameContextRef->getErrorMessage());

                    assert(notify->numFrameRequests > 0);
                    if (--notify->numFrameRequests == 0) {
                        queueTask(notify);
                        needsSort = true;
                    }
                }

                if (frameContext->external)
                    returnFrame(frameContext, f, lock);
            } else if (f) {
                for (size_t i = 0; i < frameContextRef->notifyCtxList.size(); i++) {
                    PVSFrameContext &notify = frameContextRef->notifyCtxList[i];
                    notify->availableFrames.push_back({frameContextRef->key, f});

                    assert(notify->numFrameRequests > 0);
                    if (--notify->numFrameRequests == 0) {
                        queueTask(notify);
                        needsSort = true;
                    }
                }

                if (frameContext->external)
                    returnFrame(frameContext, f, lock);
            } else if (requestedFrames) {
                // already scheduled, do nothing
            } else {
                core->logFatal("No frame returned at the end of processing by " + node->name);
            }

            if (needsSort)
                tasks.sort(taskCmp);
            break;
        }

        if (!ranTask || (activeThreads > currentMaxThreads) || (core->memory->is_over_limit() && activeThreads > 1)) {
            --activeThreads;
            if (stop) {
                lock.unlock();
                break;
            }
            
            bool shouldWait = true;

            if (++idleThreads == allThreads.size()) {
                if (flushCaches) {
                    core->clearCaches(true);
                    flushCaches = false;
                    lastCacheSweep = steadyClockNow();
                    lastShrink = lastCacheSweep; // require another full interval at the floor before flushing again
                    std::swap(tasks, altTasks);
                    deferredLog = "Pipeline flushed, resuming processing";
                    // the thread can't notify itself ahead of waiting so instead skip the wait and just loop back around to check for work immediately
                    shouldWait = false;
                    ++activeThreads;
                    --idleThreads;
                    newWork.notify_all();
                } else {
                    allIdle.notify_one();
                }
            }

            if (shouldWait) {
                // We always need to wait here to unlock the taskLock mutex, if we don't no new work can be added to the queue and the thread will never wake up again
                // Wait predicates don't work since they're the equivalent of a wrapping while loop
                do {
                    newWork.wait(lock);
                } while (activeThreads >= currentMaxThreads && !stop);
                --idleThreads;
                ++activeThreads;
            }
        }
    }
}

VSThreadPool::VSThreadPool(VSCore *core) : core(core), activeThreads(0), idleThreads(0), reqCounter(0), threadsThresh(0), lastShrink(0), lastGrow(0), overLimitSince(0), inPressureEpisode(false), lastCacheSweep(0), completedExternalFrames(0), inflightAllocation(0), processingThreads(0), cacheSweepActive(false), stopThreads(false), flushCaches(false) {
    setThreadCount(0);
}

size_t VSThreadPool::threadCount() {
    std::lock_guard<std::mutex> l(taskLock);
    return maxThreads;
}

void VSThreadPool::spawnThread() {
    std::thread *thread = new std::thread(runTasksWrapper, this, std::ref(stopThreads));
    allThreads.insert(std::make_pair(thread->get_id(), thread));
    ++activeThreads;
}

size_t VSThreadPool::setThreadCount(size_t threads) {
    bool countWarning = false;
    size_t result;
    {
        std::lock_guard<std::mutex> l(taskLock);
        maxThreads = threads > 0 ? threads : getNumAvailableThreads();
        if (maxThreads == 0) {
            maxThreads = 1;
            countWarning = true;
        }
        currentMaxThreads = maxThreads;
        threadsThresh = maxThreads;
        inPressureEpisode = false;
        overLimitSince = 0;
        result = maxThreads;
    }
    if (countWarning)
        core->logMessage(mtWarning, "Couldn't detect optimal number of threads. Thread count set to 1.");
    return result;
}

void VSThreadPool::queueTask(const PVSFrameContext &ctx) {
    assert(ctx);
    tasks.push_front(ctx);
    wakeThread();
}

void VSThreadPool::wakeThread() {
    size_t numActive = activeThreads;
    if (numActive < currentMaxThreads) {
        if (core->memory->is_over_limit() && numActive > 0) {
            // do nothing
        } else {
            if (idleThreads == 0) // newly spawned threads are active so no need to notify an additional thread
                spawnThread();
            else
                newWork.notify_one();
        }
    }
}

void VSThreadPool::startExternal(const PVSFrameContext &context) {
    assert(context);
    std::lock_guard<std::mutex> l(taskLock);
    context->reqOrder = ++reqCounter;
    context->reserveThread = context->reserveThread && (allThreads.count(std::this_thread::get_id()) > 0);
    if (context->reserveThread)
        --activeThreads;
    assert(context);
    // A reserveThread request comes from a pool thread that immediately blocks waiting for this exact
    // frame. Parking it in altTasks during a flush would deadlock: the blocked thread never becomes idle,
    // so the altTasks->tasks swap (which only fires once every pool thread is idle) can never run, so the
    // request that would unblock it is never processed. Queue those normally; only genuinely external
    // (non-pool-thread) requests can be safely deferred until the flush completes.
    if (flushCaches && !context->reserveThread) {
        altTasks.push_back(context);
    } else {
        tasks.push_back(context); // external requests can't be combined so just add to queue
        wakeThread();
    }
}

void VSThreadPool::returnFrame(VSFrameContext *rCtx, const PVSFrame &f, std::unique_lock<std::mutex> &lock) {
    assert(rCtx->frameDone);
    completedExternalFrames.fetch_add(1, std::memory_order_relaxed);
    bool outputLock = rCtx->lockOnOutput;
    bool reserveThread = rCtx->reserveThread;
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    lock.unlock();

    // don't hold on to frame references only used for processing while waiting to output
    rCtx->availableFrames.clear();

    if (rCtx->hasError()) {
        if (outputLock)
            callbackLock.lock();
        rCtx->frameDone(rCtx->userData, nullptr, rCtx->key.second, rCtx->key.first, rCtx->errorMessage.c_str());
        if (outputLock)
            callbackLock.unlock();
    } else {
        f->add_ref();
        if (outputLock)
            callbackLock.lock();

        rCtx->frameDone(rCtx->userData, f.get(), rCtx->key.second, rCtx->key.first, nullptr);
        if (outputLock)
            callbackLock.unlock();
    }
    if (core->enableFrameRefDebug)
        core->logMessage(mtInformation, core->getFrameRefInfo());
    lock.lock();
    if (reserveThread)
        ++activeThreads;
}

void VSThreadPool::startInternalRequest(const PVSFrameContext &notify, NodeOutputKey key) {
    //technically this could be done by walking up the context chain and add a new notification to the correct one
    //unfortunately this would probably be quite slow for deep scripts so just hope the cache catches it

    if (key.second < 0)
        core->logFatal("Negative frame request by: " + notify->key.first->getName());

    auto it = allContexts.find(key);
    if (it != allContexts.end()) {
        PVSFrameContext &ctx = it->second;
        ctx->notifyCtxList.push_back(notify);
        ctx->reqOrder = std::min(ctx->reqOrder, notify->reqOrder);
    } else {
        PVSFrameContext ctx = new VSFrameContext(key, notify);
        // create a new context and append it to the tasks
        allContexts.insert(std::make_pair(key, ctx));
        queueTask(ctx);
    }
}

void VSThreadPool::waitForDone() {
    std::unique_lock<std::mutex> m(taskLock);
    if (idleThreads < allThreads.size())
        allIdle.wait(m, [&] { return idleThreads == allThreads.size(); });
}

VSThreadPool::~VSThreadPool() {
    std::unique_lock<std::mutex> m(taskLock);
    stopThreads = true;

    while (!allThreads.empty()) {
        auto iter = allThreads.begin();
        auto thread = iter->second;
        newWork.notify_all();
        m.unlock();
        thread->join();
        m.lock();
        allThreads.erase(iter);
        delete thread;
        newWork.notify_all();
    }

    assert(activeThreads == 0);
    assert(idleThreads == 0);
};
