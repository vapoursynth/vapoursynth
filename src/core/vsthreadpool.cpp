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
        qFatal("Bad FPU state detected after creating new thread");
    if (!vs_isSSEStateOk())
        qFatal("Bad SSE state detected after creating new thread");
#endif

    QThreadStorage<VSThreadData *> localDataStorage;
    localDataStorage.setLocalData(new VSThreadData());

    owner->lock.lock();
	owner->activeThreads.ref();

    VSThreadData *localData = localDataStorage.localData();

    while (true) {
        bool ranTask = false;

/////////////////////////////////////////////////////////////////////////////////////////////
// Go through all tasks from the top (oldest) and process the first one possible
        for (QLinkedList<PFrameContext>::iterator iter = owner->tasks.begin(); iter != owner->tasks.end(); ++iter) {
            FrameContext *mainContext = (*iter).data();
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
                mainContext = mainContext->upstreamContext.data();
            }
            
            VSNode *clip = mainContext->clip;
            int filterMode = clip->filterMode;

/////////////////////////////////////////////////////////////////////////////////////////////
// This part handles the locking for the different filter modes

            bool parallelRequestsNeedsUnlock = false;
            if (filterMode == fmUnordered) {
                // already busy?
                if (!clip->serialMutex.tryLock())
                    continue;
            } else if (filterMode == fmSerial) {
                // already busy?
                if (!clip->serialMutex.tryLock())
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
                QMutexLocker lock(&clip->concurrentFramesMutex);
                // is the filter already processing another call for this frame? if so move along
                if (clip->concurrentFrames.contains(mainContext->n)) {
                    continue;
                } else {
                    clip->concurrentFrames.insert(mainContext->n);
                }
            } else if (filterMode == fmParallelRequests) {
                QMutexLocker lock(&clip->concurrentFramesMutex);
                // is the filter already processing another call for this frame? if so move along
                if (clip->concurrentFrames.contains(mainContext->n)) {
                    continue;
                } else {
                    // do we need the serial lock since all frames will be ready this time?
                    // check if we're in the arAllFramesReady state so we need additional locking
                    if (mainContext->numFrameRequests == 1) {
                        if (!clip->serialMutex.tryLock())
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

                Q_ASSERT(mainContext->numFrameRequests >= 0);
                mainContext->availableFrames.insert(NodeOutputKey(leafContext->clip, leafContext->n, leafContext->index), leafContext->returnedFrame);
                mainContext->lastCompletedN = leafContext->n;
                mainContext->lastCompletedNode = leafContext->node;
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Do the actual processing

            owner->lock.unlock();

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
                QMutexLocker lock(&clip->concurrentFramesMutex);
                clip->concurrentFrames.remove(mainContext->n);
            } else if (filterMode == fmParallelRequests) {
                QMutexLocker lock(&clip->concurrentFramesMutex);
                clip->concurrentFrames.remove(mainContext->n);
                if (parallelRequestsNeedsUnlock)
                    clip->serialMutex.unlock();
            }

/////////////////////////////////////////////////////////////////////////////////////////////
// Handle frames that were requested
            bool requestedFrames = !localData->isEmpty();

            owner->lock.lock();

            if (requestedFrames) {
                for (QLinkedList<PFrameContext>::iterator iter = localData->begin(); iter != localData->end(); ++iter)
                    owner->startInternal(*iter);
                localData->clear();
            }

            if (frameProcessingDone)
                owner->allContexts.remove(NodeOutputKey(mainContext->clip, mainContext->n, mainContext->index));

/////////////////////////////////////////////////////////////////////////////////////////////
// Propagate status to other linked contexts
// CHANGES mainContextRef!!!

            if (mainContext->hasError()) {
                PFrameContext n;
                do {
                    n = mainContextRef->notificationChain;

                    if (n) {
                        mainContextRef->notificationChain.clear();
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
                if (mainContext->numFrameRequests != 0 || requestedFrames)
                    qFatal("A frame was returned at the end of processing by %s but there are still outstanding requests", clip->name.constData());
                PFrameContext n;

                do {
                    n = mainContextRef->notificationChain;

                    if (n)
                        mainContextRef->notificationChain.clear();

                    if (mainContextRef->upstreamContext) {
                        mainContextRef->returnedFrame = f;
                        owner->startInternal(mainContextRef);
                    }

                    if (mainContextRef->frameDone)
                        owner->returnFrame(mainContextRef, f);
                } while ((mainContextRef = n));
            } else if (requestedFrames) {
                // already scheduled, do nothing
            } else {
				qFatal("No frame returned at the end of processing by %s", clip->name.constData());
            }
            break;
        }
   

        if ((!ranTask && !stop) || (owner->activeThreadCount() > owner->threadCount())) {
			owner->activeThreads.deref();
            owner->idleThreads.ref();
            owner->newWork.wait(&owner->lock);
            owner->idleThreads.deref();
			owner->activeThreads.ref();
        }

		if (stop) {
			owner->idleThreads.ref();
			owner->activeThreads.deref();
            owner->lock.unlock();
            return;
        }
    }
}

VSThreadPool::VSThreadPool(VSCore *core, int threads) : core(core), activeThreads(0), idleThreads(0) {
	maxThreads = threads > 0 ? threads : QThread::idealThreadCount();
	QMutexLocker m(&lock);
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
        newWork.wakeOne();
}

void VSThreadPool::releaseThread() {
	activeThreads.deref();
}

void VSThreadPool::reserveThread() {
    activeThreads.ref();
}

void VSThreadPool::notifyCaches(bool needMemory) {
    QMutexLocker lock(&core->cacheLock);
    for (int i = 0; i < core->caches.count(); i++)
        core->caches[i]->notifyCache(needMemory);
}

void VSThreadPool::start(const PFrameContext &context) {
    Q_ASSERT(context);
    QMutexLocker m(&lock);
	startInternal(context);
}

void VSThreadPool::returnFrame(const PFrameContext &rCtx, const PVideoFrame &f) {
    Q_ASSERT(rCtx->frameDone);
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    lock.unlock();
    VSFrameRef *ref = new VSFrameRef(f);
    callbackLock.lock();
    rCtx->frameDone(rCtx->userData, ref, rCtx->n, rCtx->node, NULL);
    callbackLock.unlock();
    lock.lock();
}

void VSThreadPool::returnFrame(const PFrameContext &rCtx, const QByteArray &errMsg) {
    Q_ASSERT(rCtx->frameDone);
    // we need to unlock here so the callback may request more frames without causing a deadlock
    // AND so that slow callbacks will only block operations in this thread, not all the others
    lock.unlock();
    callbackLock.lock();
    rCtx->frameDone(rCtx->userData, NULL, rCtx->n, rCtx->node, errMsg.constData());
    callbackLock.unlock();
    lock.lock();
}

void VSThreadPool::startInternal(const PFrameContext &context) {
    //technically this could be done by walking up the context chain and add a new notification to the correct one
    //unfortunately this would probably be quite slow for deep scripts so just hope the cache catches it

    if (context->n < 0)
		qFatal("Negative frame request by: %s", context->clip->getName().constData());

    // check to see if it's time to reevaluate cache sizes
    if (core->memory->isOverLimit()) {
        ticks.fetchAndStoreOrdered(0);
        notifyCaches(true);
    }

    // a normal tick for caches to adjust their sizes based on recent history
    if (!context->upstreamContext && ticks.fetchAndAddOrdered(1) == 99) {
        ticks.fetchAndStoreOrdered(0);
        notifyCaches(false);
    }

    // add it immediately if the task is to return a completed frame or report an error since it never has an existing context
    if (context->returnedFrame || context->hasError()) {
        tasks.append(context);
        wakeThread();
    } else {
        NodeOutputKey p(context->clip, context->n, context->index);

        if (allContexts.contains(p)) {
            PFrameContext ctx = allContexts[p];
            Q_ASSERT(ctx);
            Q_ASSERT(context->clip == ctx->clip && context->n == ctx->n && context->index == ctx->index);

            if (ctx->returnedFrame) {
                // special case where the requested frame is encountered "by accident"
                context->returnedFrame = ctx->returnedFrame;
                tasks.append(context);
                wakeThread();
            } else {
                // add it to the list of contexts to notify when it's available
                context->notificationChain = ctx->notificationChain;
                ctx->notificationChain = context;
            }
        } else {
            // create a new context and append it to the tasks
            if (context->upstreamContext)
                context->upstreamContext->numFrameRequests++;
            allContexts[p] = context;
            tasks.append(context);
            wakeThread();
        }
    }
}

bool VSThreadPool::isWorkerThread() {
	QMutexLocker m(&lock);
	return allThreads.contains((VSThread *)QThread::currentThread());
}

void VSThreadPool::waitForDone() {
	// todo
}

VSThreadPool::~VSThreadPool() {
	QMutexLocker m(&lock);

	// fixme, hangs on free
    while (allThreads.count()) {
        VSThread *t = *allThreads.begin();
        t->stopThread();
        newWork.wakeAll();
        m.unlock();
        t->wait();
        m.relock();
        allThreads.remove(t);
        delete t;
        newWork.wakeAll();
    }
};
