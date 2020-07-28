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
    return (a->reqOrder < b->reqOrder) || (a->reqOrder == b->reqOrder && a->n < b->n);
}

void VSThreadPool::runTasks(VSThreadPool *owner, std::atomic<bool> &stop) {
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isSSEStateOk())
        owner->core->logFatal("Bad SSE state detected after creating new thread");
#endif

    std::unique_lock<std::mutex> lock(owner->lock);

    while (true) {
        bool ranTask = false;

/////////////////////////////////////////////////////////////////////////////////////////////
// Go through all tasks from the top (oldest) and process the first one possible
        // fixme, test if this matters at all!
        std::set<VSNode *> seenNodes;

        for (auto iter = owner->tasks.begin(); iter != owner->tasks.end(); ++iter) {
            VSFrameContext *mainContext = iter->get();
            VSFrameContext *leafContext = nullptr;

/////////////////////////////////////////////////////////////////////////////////////////////
// Handle the output tasks
            if (mainContext->frameDone && mainContext->returnedFrame) {
                PVSFrameContext mainContextRef(std::move(*iter));
                owner->tasks.erase(iter);
                owner->returnFrame(mainContextRef, mainContext->returnedFrame);
                ranTask = true;
                break;
            }

            if (mainContext->frameDone && mainContext->hasError()) {
                PVSFrameContext mainContextRef(std::move(*iter));
                owner->tasks.erase(iter);
                owner->returnFrame(mainContextRef, mainContext->getErrorMessage());
                ranTask = true;
                break;
            }

            if (mainContext->returnedFrame || mainContext->hasError()) {
                leafContext = mainContext;
                mainContext = mainContext->upstreamContext.get();
            }

            VSNode *clip = mainContext->clip;
            int filterMode = clip->filterMode;

/////////////////////////////////////////////////////////////////////////////////////////////
// Fast path for arFrameReady events that don't need to be notified

            if ((!clip->frameReadyNotify && (mainContext->numFrameRequests > 1) && (!leafContext || !leafContext->hasError()))) {
                mainContext->availableFrames.push_back(std::make_pair(NodeOutputKey(leafContext->clip, leafContext->n, leafContext->index), leafContext->returnedFrame));
                --mainContext->numFrameRequests;
                owner->tasks.erase(iter);
                ranTask = true;
                break;
            }

            // Don't try to lock the same node twice since it's likely to fail and will produce more out of order requests as well
            if (!seenNodes.insert(mainContext->clip).second)
                continue;

/////////////////////////////////////////////////////////////////////////////////////////////
// This part handles the locking for the different filter modes


            // Does the filter need the per instance mutex? fmSerial, fmUnordered and fmParallelRequests (when in the arAllFramesReady state) use this
            bool useSerialLock = (filterMode == fmSerial || filterMode == fmUnordered || filterMode == fmUnorderedLinear || (filterMode == fmParallelRequests && mainContext->numFrameRequests == 1));

            // Guard against multiple arFrameReady calls into the same instance for the same frame, without this plugin writers would need to hold a mutex to modify the per frame data
            // Only needed due to the arFrameReady events and nothing else
            bool useConcurrentCheck = (clip->frameReadyNotify && ((filterMode == fmParallel || filterMode == fmParallelRequests) && mainContext->numFrameRequests > 1));

            // Note that technically useSerialLock^useConcurrentCheck has to be true BUT due to the initial request (mainContext->numFrameRequests == 0) case
            // not starting its requests until after the arInitial call returns no lock or bookkeeping of it is actually needed since it can't call into the filter at the same time as arFrameReady
            assert(!(useSerialLock && useConcurrentCheck));

            if (useSerialLock) {
                if (!clip->serialMutex.try_lock())
                    continue;
                if (filterMode == fmSerial) {
                    if (clip->serialFrame == -1) {
                        clip->serialFrame = mainContext->n;
                        // another frame already in progress?
                    } else if (clip->serialFrame != mainContext->n) {
                        clip->serialMutex.unlock();
                        continue;
                    }
                }
            }

            if (useConcurrentCheck) {
                std::lock_guard<std::mutex> lock(clip->concurrentFramesMutex);
                // is the filter already processing another call for this frame? if so move along
                if (!clip->concurrentFrames.insert(mainContext->n).second)
                    continue;
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Remove the context from the task list and keep references around until processing is done

            PVSFrameContext mainContextRef;
            PVSFrameContext leafContextRef;
            if (leafContext) {
                leafContextRef = std::move(*iter);
                mainContextRef = leafContextRef->upstreamContext;
            } else {
                mainContextRef = std::move(*iter);
            }

            owner->tasks.erase(iter);

/////////////////////////////////////////////////////////////////////////////////////////////
// Figure out the activation reason

            VSActivationReason ar = arInitial;
            bool skipCall = false; // Used to avoid multiple error calls for the same frame request going into a filter
            if ((leafContext && leafContext->hasError()) || mainContext->hasError()) {
                ar = arError;
                skipCall = mainContext->setError(leafContext->getErrorMessage());
                --mainContext->numFrameRequests;
            } else if (leafContext && leafContext->returnedFrame) {
                if (--mainContext->numFrameRequests > 0)
                    ar = static_cast<VSActivationReason>(vs3::arFrameReady);
                else
                    ar = arAllFramesReady;

                mainContext->availableFrames.push_back(std::make_pair(NodeOutputKey(leafContext->clip, leafContext->n, leafContext->index), leafContext->returnedFrame));
                mainContext->lastCompletedN = leafContext->n;
                mainContext->lastCompletedNode = leafContext->node;
            }

            bool hasExistingRequests = !!mainContext->numFrameRequests;
            bool isLinear = (filterMode == fmUnorderedLinear);

/////////////////////////////////////////////////////////////////////////////////////////////
// Do the actual processing

            if (!isLinear)
                lock.unlock();

            assert(ar == arError || !mainContext->hasError());
#ifdef VS_FRAME_REQ_DEBUG
            vsWarning("Entering: %s Frame: %d Index: %d AR: %d Req: %d", mainContext->clip->name.c_str(), mainContext->n, mainContext->index, (int)ar, (int)mainContext->reqOrder);
#endif
            PVSFrameRef f;
            if (!skipCall)
                f = clip->getFrameInternal(mainContext->n, ar, mainContext);
            ranTask = true;
#ifdef VS_FRAME_REQ_DEBUG
            vsWarning("Exiting: %s Frame: %d Index: %d AR: %d Req: %d", mainContext->clip->name.c_str(), mainContext->n, mainContext->index, (int)ar, (int)mainContext->reqOrder);
#endif
            bool frameProcessingDone = f || mainContext->hasError();
            if (mainContext->hasError() && f)
                owner->core->logFatal("A frame was returned by " + clip->name + " but an error was also set, this is not allowed");

/////////////////////////////////////////////////////////////////////////////////////////////
// Unlock so the next job can run on the context
            if (useSerialLock) {
                if (filterMode == fmSerial && frameProcessingDone)
                    clip->serialFrame = -1;
                clip->serialMutex.unlock();
            }

            if (useConcurrentCheck) {
                std::lock_guard<std::mutex> lock(clip->concurrentFramesMutex);
                clip->concurrentFrames.erase(mainContext->n);
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Handle frames that were requested
            bool requestedFrames = mainContext->reqList.size() > 0 && !frameProcessingDone;
            bool needsSort = requestedFrames;

            if (!isLinear)
                lock.lock();

            if (requestedFrames) {
                for (size_t i = 0; i < mainContext->reqList.size(); i++)
                    owner->startInternal(mainContext->reqList[i]);
                mainContext->reqList.clear();
            }

            if (frameProcessingDone)
                owner->allContexts.erase(NodeOutputKey(mainContext->clip, mainContext->n, mainContext->index));

/////////////////////////////////////////////////////////////////////////////////////////////
// Propagate status to other linked contexts
// CHANGES mainContextRef!!!

            if (mainContext->hasError() && !hasExistingRequests && !requestedFrames) {
                PVSFrameContext n;
                do {
                    n = mainContextRef->notificationChain;

                    if (n) {
                        mainContextRef->notificationChain.reset();
                        n->setError(mainContextRef->getErrorMessage());
                    }

                    if (mainContextRef->upstreamContext) {
                        owner->startInternal(mainContextRef);
                        needsSort = true;
                    }

                    if (mainContextRef->frameDone) {
                        owner->returnFrame(mainContextRef, mainContextRef->getErrorMessage());
                    }
                } while ((mainContextRef = n));
            } else if (f) {
                if (hasExistingRequests || requestedFrames)
                    owner->core->logFatal("A frame was returned at the end of processing by " + clip->name + " but there are still outstanding requests");
                PVSFrameContext n;

                do {
                    n = mainContextRef->notificationChain;

                    if (n)
                        mainContextRef->notificationChain.reset();

                    if (mainContextRef->upstreamContext) {
                        mainContextRef->returnedFrame = f;
                        owner->startInternal(mainContextRef);
                        needsSort = true;
                    }

                    if (mainContextRef->frameDone)
                        owner->returnFrame(mainContextRef, f);
                } while ((mainContextRef = n));
            } else if (hasExistingRequests || requestedFrames) {
                // already scheduled, do nothing
            } else {
                owner->core->logFatal("No frame returned at the end of processing by " + clip->name);
            }

            if (needsSort)
                owner->tasks.sort(taskCmp);
            break;
        }


        if (!ranTask || owner->activeThreads > owner->maxThreads) {
            --owner->activeThreads;
            if (stop) {
                lock.unlock();
                break;
            }
            if (++owner->idleThreads == owner->allThreads.size())
                owner->allIdle.notify_one();

            owner->newWork.wait(lock);
            --owner->idleThreads;
            ++owner->activeThreads;
        }
    }
}

VSThreadPool::VSThreadPool(VSCore *core) : core(core), activeThreads(0), idleThreads(0), reqCounter(0), stopThreads(false), ticks(0) {
    setThreadCount(0);
}

size_t VSThreadPool::threadCount() {
    std::lock_guard<std::mutex> l(lock);
    return maxThreads;
}

void VSThreadPool::spawnThread() {
    std::thread *thread = new std::thread(runTasks, this, std::ref(stopThreads));
    allThreads.insert(std::make_pair(thread->get_id(), thread));
    ++activeThreads;
}

size_t VSThreadPool::setThreadCount(size_t threads) {
    std::lock_guard<std::mutex> l(lock);
    maxThreads = threads > 0 ? threads : getNumAvailableThreads();
    if (maxThreads == 0) {
        maxThreads = 1;
        core->logMessage(mtWarning, "Couldn't detect optimal number of threads. Thread count set to 1.");
    }
    return maxThreads;
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

void VSThreadPool::notifyCaches(bool needMemory) {
    std::lock_guard<std::mutex> lock(core->cacheLock);
    for (auto &cache : core->caches)
        cache->notifyCache(needMemory);
}

void VSThreadPool::start(const PVSFrameContext &context) {
    assert(context);
    std::lock_guard<std::mutex> l(lock);
    context->reqOrder = ++reqCounter;
    startInternal(context);
}

void VSThreadPool::returnFrame(const PVSFrameContext &rCtx, const PVSFrameRef &f) {
    assert(rCtx->frameDone);
    bool outputLock = rCtx->lockOnOutput;
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    lock.unlock();
    f->add_ref();
    if (outputLock)
        callbackLock.lock();
    rCtx->frameDone(rCtx->userData, f.get(), rCtx->n, rCtx->node, nullptr);
    if (outputLock)
        callbackLock.unlock();
    lock.lock();
}

void VSThreadPool::returnFrame(const PVSFrameContext &rCtx, const std::string &errMsg) {
    assert(rCtx->frameDone);
    bool outputLock = rCtx->lockOnOutput;
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    lock.unlock();
    if (outputLock)
        callbackLock.lock();
    rCtx->frameDone(rCtx->userData, nullptr, rCtx->n, rCtx->node, errMsg.c_str());
    if (outputLock)
        callbackLock.unlock();
    lock.lock();
}

void VSThreadPool::startInternal(const PVSFrameContext &context) {
    //technically this could be done by walking up the context chain and add a new notification to the correct one
    //unfortunately this would probably be quite slow for deep scripts so just hope the cache catches it

    if (context->n < 0)
        core->logFatal("Negative frame request by: " + context->upstreamContext->clip->getName());

    // check to see if it's time to reevaluate cache sizes
    if (core->memory->isOverLimit()) {
        ticks = 0;
        notifyCaches(true);
    } else if (!context->upstreamContext && ++ticks == 500) { // a normal tick for caches to adjust their sizes based on recent history
        ticks = 0;
        notifyCaches(false);
    }

    // add it immediately if the task is to return a completed frame or report an error since it never has an existing context
    if (context->returnedFrame || context->hasError()) {
        tasks.push_back(context);
    } else {
        if (context->upstreamContext)
            ++context->upstreamContext->numFrameRequests;

        NodeOutputKey p(context->clip, context->n, context->index);
        
        auto it = allContexts.find(p);
        if (it != allContexts.end()) {
            PVSFrameContext &ctx = it->second;
            assert(context->clip == ctx->clip && context->n == ctx->n && context->index == ctx->index);

            if (ctx->returnedFrame) {
                // special case where the requested frame is encountered "by accident"
                context->returnedFrame = ctx->returnedFrame;
                tasks.push_back(context);
            } else {
                // add it to the list of contexts to notify when it's available
                context->notificationChain = ctx->notificationChain;
                ctx->notificationChain = context;
                ctx->reqOrder = std::min(ctx->reqOrder, context->reqOrder);
            }
        } else {
            // create a new context and append it to the tasks
            allContexts.insert(std::make_pair(p, context));
            tasks.push_back(context);
        }
    }
    wakeThread();
}

bool VSThreadPool::isWorkerThread() {
    std::lock_guard<std::mutex> m(lock);
    return allThreads.count(std::this_thread::get_id()) > 0;
}

void VSThreadPool::waitForDone() {
    std::unique_lock<std::mutex> m(lock);
    if (idleThreads < allThreads.size())
        allIdle.wait(m);
}

VSThreadPool::~VSThreadPool() {
    std::unique_lock<std::mutex> m(lock);
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
