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

void VSThreadPool::runTasksWrapper(VSThreadPool *owner, std::atomic<bool> &stop) {
    owner->runTasks(stop);
}

void VSThreadPool::runTasks(std::atomic<bool> &stop) {
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        core->logFatal("Bad SSE state detected after creating new thread");
#endif

    std::unique_lock<std::mutex> lock(taskLock);

    while (true) {
        bool ranTask = false;

/////////////////////////////////////////////////////////////////////////////////////////////
// Go through all tasks from the top (oldest) and process the first one possible

        std::set<VSNode *> seenNodes;

        for (auto iter = tasks.begin(); iter != tasks.end(); ++iter) {
            VSFrameContext *frameContext = iter->get();
            VSNode *node = frameContext->key.first;

/////////////////////////////////////////////////////////////////////////////////////////////
// Fast path if a frame is cached

            if (node->cacheEnabled) {
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

                    PVSFrameContext mainContextRef = std::move(*iter);
                    tasks.erase(iter);
                    allContexts.erase(frameContext->key);

                    if (frameContext->external)
                        returnFrame(frameContext, f);

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
            if (filterMode != fmFrameState && !seenNodes.insert(node).second)
                continue;

            // Does the filter need the per instance mutex? fmFrameState, fmUnordered and fmParallelRequests (when in the arAllFramesReady state) use this
            bool useSerialLock = (filterMode == fmFrameState || filterMode == fmUnordered || (filterMode == fmParallelRequests && !frameContext->first));

            if (useSerialLock) {
                if (!node->serialMutex.try_lock())
                    continue;
                if (filterMode == fmFrameState) {
                    if (node->serialFrame == -1) {
                        node->serialFrame = frameContext->key.second;
                        // another frame already in progress?
                    } else if (node->serialFrame != frameContext->key.second) {
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
            } else if (frameContext->first) {
                frameContext->first = false;
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Do the actual processing

            lock.unlock();

            PVSFrame f = node->getFrameInternal(frameContext->key.second, ar, frameContext);
            ranTask = true;

            bool frameProcessingDone = f || frameContext->hasError();
            if (frameContext->hasError() && f)
                core->logFatal("A frame was returned by " + node->name + " but an error was also set, this is not allowed");

/////////////////////////////////////////////////////////////////////////////////////////////
// Unlock so the next job can run on the context
            if (useSerialLock) {
                if (frameProcessingDone && filterMode == fmFrameState)
                    node->serialFrame = -1;
                node->serialMutex.unlock();
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Handle frames that were requested
            bool requestedFrames = frameContext->reqList.size() > 0 && !frameProcessingDone;
            bool needsSort = false;
            if (f && requestedFrames)
                core->logFatal("A frame was returned at the end of processing by " + node->name + " but there are still outstanding requests");

            lock.lock();

            if (requestedFrames) {
                assert(frameContext->numFrameRequests == 0);

                for (size_t i = 0; i < frameContext->reqList.size(); i++)
                    startInternalRequest(frameContextRef, frameContext->reqList[i]);

                frameContext->numFrameRequests = frameContext->reqList.size();
                frameContext->reqList.clear();
            }

            if (frameProcessingDone)
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
                    returnFrame(frameContext, f);
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
                    returnFrame(frameContext, f);
            } else if (requestedFrames) {
                // already scheduled, do nothing
            } else {
                core->logFatal("No frame returned at the end of processing by " + node->name);
            }

            if (needsSort)
                tasks.sort(taskCmp);
            break;
        }

        if (!ranTask || activeThreads > maxThreads) {
            --activeThreads;
            if (stop) {
                lock.unlock();
                break;
            }
            if (++idleThreads == allThreads.size())
                allIdle.notify_one();

            newWork.wait(lock);
            --idleThreads;
            ++activeThreads;
        }
    }
}

VSThreadPool::VSThreadPool(VSCore *core) : core(core), activeThreads(0), idleThreads(0), reqCounter(0), stopThreads(false), ticks(0) {
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
    std::lock_guard<std::mutex> l(taskLock);
    maxThreads = threads > 0 ? threads : getNumAvailableThreads();
    if (maxThreads == 0) {
        maxThreads = 1;
        core->logMessage(mtWarning, "Couldn't detect optimal number of threads. Thread count set to 1.");
    }
    return maxThreads;
}

void VSThreadPool::queueTask(const PVSFrameContext &ctx) {
    assert(ctx);
    tasks.push_front(ctx);
    wakeThread();
}

void VSThreadPool::wakeThread() {
    if (activeThreads < maxThreads) {
        if (idleThreads == 0) // newly spawned threads are active so no need to notify an additional thread
            spawnThread();
        else
            newWork.notify_one();
    }
}

void VSThreadPool::releaseThread() {
    --activeThreads;
}

void VSThreadPool::reserveThread() {
    ++activeThreads;
}

void VSThreadPool::startExternal(const PVSFrameContext &context) {
    assert(context);
    std::lock_guard<std::mutex> l(taskLock);
    context->reqOrder = ++reqCounter;
    assert(context);
    tasks.push_back(context); // external requests can't be combined so just add to queue
    wakeThread();
}

void VSThreadPool::returnFrame(const VSFrameContext *rCtx, const PVSFrame &f) {
    assert(rCtx->frameDone);
    bool outputLock = rCtx->lockOnOutput;
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    taskLock.unlock();
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
    taskLock.lock();
}

void VSThreadPool::startInternalRequest(const PVSFrameContext &notify, NodeOutputKey key) {
    //technically this could be done by walking up the context chain and add a new notification to the correct one
    //unfortunately this would probably be quite slow for deep scripts so just hope the cache catches it

    if (key.second < 0)
        core->logFatal("Negative frame request by: " + notify->key.first->getName());

    // check to see if it's time to reevaluate cache sizes
    if (core->memory->is_over_limit()) {
        ticks = 0;
        core->notifyCaches(true);
    } else if (++ticks == 500) { // a normal tick for caches to adjust their sizes based on recent history
        ticks = 0;
        core->notifyCaches(false);
    }


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

bool VSThreadPool::isWorkerThread() {
    std::lock_guard<std::mutex> m(taskLock);
    return allThreads.count(std::this_thread::get_id()) > 0;
}

void VSThreadPool::waitForDone() {
    std::unique_lock<std::mutex> m(taskLock);
    if (idleThreads < allThreads.size())
        allIdle.wait(m);
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
