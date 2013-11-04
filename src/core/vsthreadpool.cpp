/*
* Copyright (c) 2012-2013 Fredrik Mellbin
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
#include <assert.h>
#ifdef VS_TARGET_CPU_X86
#include "x86utils.h"
#endif

VSThread::VSThread(VSThreadPool *owner) : owner(owner), stop(false) {

}

void VSThread::stopThread() {
    stop = true;
}

void VSThread::run() {
#ifdef VS_TARGET_OS_WINDOWS
    if (!vs_isMMXStateOk())
        qFatal("Bad MMX state detected after creating new thread");
    if (!vs_isFPUStateOk())
        qWarning("Bad FPU state detected after creating new thread");
    if (!vs_isSSEStateOk())
        qFatal("Bad SSE state detected after creating new thread");
#endif

    QThreadStorage<VSThreadData *> localDataStorage;
    localDataStorage.setLocalData(new VSThreadData());

    std::unique_lock<std::mutex> lock(owner->lock);
    ++owner->activeThreads;

    VSThreadData *localData = localDataStorage.localData();

    while (true) {
        bool ranTask = false;

/////////////////////////////////////////////////////////////////////////////////////////////
// Go through all tasks from the top (oldest) and process the first one possible
        for (std::list<PFrameContext>::iterator iter = owner->tasks.begin(); iter != owner->tasks.end(); ++iter) {
            FrameContext *mainContext = iter->get();
            FrameContext *leafContext = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////
// Handle the output tasks
            if (mainContext->frameDone && mainContext->returnedFrame) {
                PFrameContext mainContextRef(*iter);
                owner->tasks.erase(iter);
                owner->returnFrame(mainContextRef, mainContext->returnedFrame);
                ranTask = true;
                break;
            }

            if (mainContext->frameDone && mainContext->hasError()) {
                PFrameContext mainContextRef(*iter);
                owner->tasks.erase(iter);
                owner->returnFrame(mainContextRef, mainContext->getErrorMessage());
                ranTask = true;
                break;
            }

            bool hasLeafContext = mainContext->returnedFrame || mainContext->hasError();
            if (hasLeafContext)
            {
                leafContext = mainContext;
                mainContext = mainContext->upstreamContext.get();
            }

            VSNode *clip = mainContext->clip;
            int filterMode = clip->filterMode;

/////////////////////////////////////////////////////////////////////////////////////////////
// This part handles the locking for the different filter modes

            bool parallelRequestsNeedsUnlock = false;
            if (filterMode == fmUnordered) {
                // already busy?
                if (!clip->serialMutex.try_lock())
                    continue;
            } else if (filterMode == fmSerial) {
                // already busy?
                if (!clip->serialMutex.try_lock())
                    continue;
                // no frame in progress?
                if (clip->serialFrame == -1) {
                    clip->serialFrame = mainContext->n;
                //
                } else if (clip->serialFrame != mainContext->n) {
                    clip->serialMutex.unlock();
                    continue;
                }
                // continue processing the already started frame
            } else if (filterMode == fmParallel) {
                std::lock_guard<std::mutex> lock(clip->concurrentFramesMutex);
                // is the filter already processing another call for this frame? if so move along
                if (clip->concurrentFrames.count(mainContext->n)) {
                    continue;
                } else {
                    clip->concurrentFrames.insert(mainContext->n);
                }
            } else if (filterMode == fmParallelRequests) {
                std::lock_guard<std::mutex> lock(clip->concurrentFramesMutex);
                // is the filter already processing another call for this frame? if so move along
                if (clip->concurrentFrames.count(mainContext->n)) {
                    continue;
                } else {
                    // do we need the serial lock since all frames will be ready this time?
                    // check if we're in the arAllFramesReady state so we need additional locking
                    if (mainContext->numFrameRequests == 1) {
                        if (!clip->serialMutex.try_lock())
                            continue;
                        parallelRequestsNeedsUnlock = true;
                        clip->concurrentFrames.insert(mainContext->n);
                    }
                }
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Remove the context from the task list

            PFrameContext mainContextRef;
            PFrameContext leafContextRef;
            if (hasLeafContext) {
                leafContextRef = *iter;
                mainContextRef = leafContextRef->upstreamContext;
            } else {
                mainContextRef = *iter;
            }

            owner->tasks.erase(iter);

/////////////////////////////////////////////////////////////////////////////////////////////
// Figure out the activation reason

            VSActivationReason ar = arInitial;
            if (hasLeafContext && leafContext->hasError()) {
                ar = arError;
                mainContext->setError(leafContext->getErrorMessage());
            } else if (hasLeafContext && leafContext->returnedFrame) {
                if (--mainContext->numFrameRequests > 0)
                    ar = arFrameReady;
                else
                    ar = arAllFramesReady;

                assert(mainContext->numFrameRequests >= 0);
                mainContext->availableFrames.insert(std::pair<NodeOutputKey, PVideoFrame>(NodeOutputKey(leafContext->clip, leafContext->n, leafContext->index), leafContext->returnedFrame));
                mainContext->lastCompletedN = leafContext->n;
                mainContext->lastCompletedNode = leafContext->node;
            }

            bool hasExistingRequests = !!mainContext->numFrameRequests;

/////////////////////////////////////////////////////////////////////////////////////////////
// Do the actual processing

            lock.unlock();

            mainContext->tlRequests = &localDataStorage;

            PVideoFrame f(clip->getFrameInternal(mainContext->n, ar, mainContextRef));
            ranTask = true;
            bool frameProcessingDone = f || mainContext->hasError();

/////////////////////////////////////////////////////////////////////////////////////////////
// Unlock so the next job can run on the context
            if (filterMode == fmUnordered) {
                clip->serialMutex.unlock();
            } else if (filterMode == fmSerial) {
                if (frameProcessingDone)
                    clip->serialFrame = -1;
                clip->serialMutex.unlock();
            } else if (filterMode == fmParallel) {
                std::lock_guard<std::mutex> lock(clip->concurrentFramesMutex);
                clip->concurrentFrames.erase(mainContext->n);
            } else if (filterMode == fmParallelRequests) {
                std::lock_guard<std::mutex> lock(clip->concurrentFramesMutex);
                clip->concurrentFrames.erase(mainContext->n);
                if (parallelRequestsNeedsUnlock)
                    clip->serialMutex.unlock();
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Handle frames that were requested
            bool requestedFrames = !localData->empty();

            lock.lock();

            if (requestedFrames) {
                for (auto &reqIter : *localData)
                    owner->startInternal(reqIter);
                localData->clear();
            }

            if (frameProcessingDone)
                owner->allContexts.erase(NodeOutputKey(mainContext->clip, mainContext->n, mainContext->index));

/////////////////////////////////////////////////////////////////////////////////////////////
// Propagate status to other linked contexts
// CHANGES mainContextRef!!!

            if (mainContext->hasError()) {
                PFrameContext n;
                do {
                    n = mainContextRef->notificationChain;

                    if (n) {
                        mainContextRef->notificationChain.reset();
                        n->setError(mainContextRef->getErrorMessage());
                    }

                    if (mainContextRef->upstreamContext) {
                        owner->startInternal(mainContextRef);
                    }

                    if (mainContextRef->frameDone) {
                        owner->returnFrame(mainContextRef, mainContextRef->getErrorMessage());
                    }
                } while ((mainContextRef = n));
            } else if (f) {
                if (hasExistingRequests || requestedFrames)
                    qFatal("A frame was returned at the end of processing by %s but there are still outstanding requests", clip->name.c_str());
                PFrameContext n;

                do {
                    n = mainContextRef->notificationChain;

                    if (n)
                        mainContextRef->notificationChain.reset();

                    if (mainContextRef->upstreamContext) {
                        mainContextRef->returnedFrame = f;
                        owner->startInternal(mainContextRef);
                    }

                    if (mainContextRef->frameDone)
                        owner->returnFrame(mainContextRef, f);
                } while ((mainContextRef = n));
            } else if (hasExistingRequests || requestedFrames) {
                // already scheduled, do nothing
            } else {
                qFatal("No frame returned at the end of processing by %s", clip->name.c_str());
            }
            break;
        }


        if ((!ranTask && !stop) || (owner->activeThreadCount() > owner->threadCount())) {
            --owner->activeThreads;
            ++owner->idleThreads;
            owner->newWork.wait(lock);
            --owner->idleThreads;
            ++owner->activeThreads;
        }

        if (stop) {
            --owner->idleThreads;
            ++owner->activeThreads;
            lock.unlock();
            return;
        }
    }
}

VSThreadPool::VSThreadPool(VSCore *core, int threads) : core(core), activeThreads(0), idleThreads(0) {
    maxThreads = threads > 0 ? threads : QThread::idealThreadCount();
}

int VSThreadPool::activeThreadCount() const {
    return activeThreads;
}

int VSThreadPool::threadCount() const {
    return maxThreads;
}

void VSThreadPool::spawnThread() {
    VSThread *vst = new VSThread(this);
    allThreads.insert(vst);
    vst->start();
}

void VSThreadPool::setThreadCount(int threads) {
    maxThreads = threads;
}

void VSThreadPool::wakeThread() {
    if (activeThreads < maxThreads && idleThreads == 0)
        spawnThread();

    if (activeThreads < maxThreads)
        newWork.notify_one();
}

void VSThreadPool::releaseThread() {
    --activeThreads;
}

void VSThreadPool::reserveThread() {
    --activeThreads;
}

void VSThreadPool::notifyCaches(bool needMemory) {
    std::lock_guard<std::mutex> lock(core->cacheLock);
    for (int i = 0; i < core->caches.count(); i++)
        core->caches[i]->notifyCache(needMemory);
}

void VSThreadPool::start(const PFrameContext &context) {
    assert(context);
    std::lock_guard<std::mutex> lock(lock);
    startInternal(context);
}

void VSThreadPool::returnFrame(const PFrameContext &rCtx, const PVideoFrame &f) {
    assert(rCtx->frameDone);
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    lock.unlock();
    VSFrameRef *ref = new VSFrameRef(f);
    callbackLock.lock();
    rCtx->frameDone(rCtx->userData, ref, rCtx->n, rCtx->node, NULL);
    callbackLock.unlock();
    lock.lock();
}

void VSThreadPool::returnFrame(const PFrameContext &rCtx, const std::string &errMsg) {
    assert(rCtx->frameDone);
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    lock.unlock();
    callbackLock.lock();
    rCtx->frameDone(rCtx->userData, NULL, rCtx->n, rCtx->node, errMsg.c_str());
    callbackLock.unlock();
    lock.lock();
}

void VSThreadPool::startInternal(const PFrameContext &context) {
    //technically this could be done by walking up the context chain and add a new notification to the correct one
    //unfortunately this would probably be quite slow for deep scripts so just hope the cache catches it

    if (context->n < 0)
        qFatal("Negative frame request by: %s", context->clip->getName().c_str());

    // check to see if it's time to reevaluate cache sizes
    if (core->memory->isOverLimit()) {
        ticks = 0;
        notifyCaches(true);
    }

    // a normal tick for caches to adjust their sizes based on recent history
    if (!context->upstreamContext && ++ticks == 100) {
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

        if (allContexts.count(p)) {
            PFrameContext &ctx = allContexts[p];
            assert(context->clip == ctx->clip && context->n == ctx->n && context->index == ctx->index);

            if (ctx->returnedFrame) {
                // special case where the requested frame is encountered "by accident"
                context->returnedFrame = ctx->returnedFrame;
                tasks.push_back(context);
            } else {
                // add it to the list of contexts to notify when it's available
                context->notificationChain = ctx->notificationChain;
                ctx->notificationChain = context;
            }
        } else {
            // create a new context and append it to the tasks
            allContexts[p] = context;
            tasks.push_back(context);
        }
    }
    wakeThread();
}

bool VSThreadPool::isWorkerThread() {
    std::lock_guard<std::mutex> m(lock);
    return allThreads.count((VSThread *)QThread::currentThread());
}

void VSThreadPool::waitForDone() {
    // todo
}

VSThreadPool::~VSThreadPool() {
    std::unique_lock<std::mutex> m(lock);

    // fixme, hangs on free
    while (!allThreads.empty()) {
        VSThread *t = *allThreads.begin();
        t->stopThread();
        newWork.notify_all();
        m.unlock();
        t->wait();
        m.lock();
        allThreads.erase(t);
        delete t;
        newWork.notify_all();
    }
};
