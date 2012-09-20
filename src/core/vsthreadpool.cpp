/*
* Copyright (c) 2012 Fredrik Mellbin
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
* License along with Libav; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "vscore.h"

VSThread::VSThread(VSThreadPool *owner) : owner(owner), stop(false) {

}

void VSThread::stopThread() {
    stop = true;
}

void VSThread::run() {
    owner->lock.lock();
    owner->activeThreads++;

    while (true) {
        bool ranTask = false;

        for (int i = 0; i < owner->tasks.count(); i++) {
            PFrameContext rCtx = owner->tasks[i];

            if (rCtx->frameDone && rCtx->returnedFrame) {
                owner->tasks.removeAt(i--);
                owner->returnFrame(rCtx, rCtx->returnedFrame);
                ranTask = true;
            } else {

                PFrameContext pCtx = rCtx;

                if (rCtx->returnedFrame || rCtx->hasError())
                    rCtx = rCtx->upstreamContext;

                Q_ASSERT(rCtx);
                Q_ASSERT(pCtx);

                // if an error has already been flagged upstream simply drop this task so a filter won't get multiple arError calls for the same frame
                if (rCtx->hasError()) {
                    owner->tasks.removeAt(i--);
                    continue;
                }

                bool isSingleInstance = rCtx->clip->filterMode == fmSerial || rCtx->clip->filterMode == fmParallelRequests;

                // this check is common for both filter modes, it makes sure that multiple calls won't be made in parallel to a single filter to produce the same frame
                // special casing so serial unordered doesn't need yet another list
                if (owner->runningTasks.contains(FrameKey(rCtx->clip, rCtx->clip->filterMode == fmUnordered ? -1 : rCtx->n)))
                    continue;

                if (isSingleInstance) {
                    // this is the complicated case, a new frame may not be started until all calls are completed for the current one
                    if (owner->framesInProgress.contains(rCtx->clip) && owner->framesInProgress[rCtx->clip] != rCtx->n)
                        continue;
                }

                // mark task as active
                owner->tasks.removeAt(i--);
                owner->runningTasks.insert(FrameKey(rCtx->clip, rCtx->clip->filterMode == fmUnordered ? -1 : rCtx->n), rCtx);

                if ((rCtx->clip->filterMode == fmParallelRequests && pCtx->returnedFrame) || (rCtx->clip->filterMode == fmSerial))
                    owner->framesInProgress.insert(rCtx->clip, rCtx->n);

                owner->lock.unlock();
                // run task

                ActivationReason ar = arInitial;

                if (pCtx->hasError()) {
                    ar = arError;
                    rCtx->setError(pCtx->getErrorMessage());
                } else if (pCtx != rCtx && pCtx->returnedFrame) {
                    if (rCtx->numFrameRequests.deref())
                        ar = arFrameReady;
                    else
                        ar = arAllFramesReady;

                    Q_ASSERT(rCtx->numFrameRequests >= 0);
                    rCtx->availableFrames.insert(FrameKey(pCtx->clip, pCtx->n), pCtx->returnedFrame);
                    rCtx->lastCompletedN = pCtx->n;
                    rCtx->lastCompletedNode = pCtx->node;
                }

                PVideoFrame f = rCtx->clip->getFrameInternal(rCtx->n, ar, rCtx);
                ranTask = true;
                owner->lock.lock();

                if (f && rCtx->numFrameRequests > 0)
                    qFatal("Frame returned but there are still pending frame requests, filter: " + rCtx->clip->name);

                /*
                PFrameContext a = pCtx;
                char b[100];
                if (a->returnedFrame)
                	a = a->upstreamContext;
                if (rCtx->clip->name == "FFVideoSource") {
                	sprintf(b, "src:\t%d\n", a->n);
                	OutputDebugStringA(b);
                } else if (f) {
                	sprintf(b, "%s:\t%d\t%d\t%d\n", rCtx->clip->name.constData(), ((int)a->clip) & 0xff, a->n, (int)((bool)f));
                	OutputDebugStringA(b);
                }
                */

                owner->runningTasks.remove(FrameKey(rCtx->clip, rCtx->clip->filterMode == fmUnordered ? -1 : rCtx->n));

				if (f || ar == arError || rCtx->hasError()) {
                    // free all input frames quickly since the frame processing is done
                    rCtx->availableFrames.clear();

                    if (isSingleInstance) {
                        if (owner->framesInProgress[rCtx->clip] != rCtx->n && !rCtx->hasError())
                            qWarning("Releasing unobtained frame lock");

                        owner->framesInProgress.remove(rCtx->clip);
                    }

                    owner->allContexts.remove(FrameKey(rCtx->clip, rCtx->n));
                }

                if (rCtx->hasError()) {
                    PFrameContext n;

                    do {
                        n = rCtx->notificationChain;

                        if (n) {
                            rCtx->notificationChain.clear();
                            n->setError(rCtx->getErrorMessage());
                        }

                        if (rCtx->upstreamContext) {
                            owner->startInternal(rCtx);
                        }

                        if (rCtx->frameDone) {
                            owner->lock.unlock();
                            QMutexLocker callbackLock(&owner->callbackLock);
                            rCtx->frameDone(rCtx->userData, NULL, rCtx->n, rCtx->node, rCtx->getErrorMessage().constData());
                            callbackLock.unlock();
                            owner->lock.lock();
                        }
                    } while ((rCtx = n));
                } else if (f) {
                    Q_ASSERT(rCtx->numFrameRequests == 0);
                    PFrameContext n;

                    do {
                        n = rCtx->notificationChain;

                        if (n)
                            rCtx->notificationChain.clear();

                        if (rCtx->upstreamContext) {
                            rCtx->returnedFrame = f;
                            owner->startInternal(rCtx);
                        }

                        if (rCtx->frameDone)
                            owner->returnFrame(rCtx, f);
                    } while ((rCtx = n));
                } else if (rCtx->numFrameRequests > 0 || rCtx->n < 0) {
                    // already scheduled or in the case of negative n it is simply a cache notify message
                } else {
                    qFatal("No frame returned at the end of processing by " + rCtx->clip->name);
                }

                break;
            }
        }

        if (!ranTask && !stop) {
            owner->activeThreads--;
            owner->newWork.wait(&owner->lock);
            owner->activeThreads++;
        } else if (stop) {
            owner->activeThreads--;
            owner->lock.unlock();
            return;
        }
    }
}

VSThreadPool::VSThreadPool(VSCore *core, int threadCount) : core(core), activeThreads(0) {
    setMaxThreadCount(threadCount);
}

int	VSThreadPool::activeThreadCount() const {
    return activeThreads;
}

int	VSThreadPool::threadCount() const {
    return allThreads.count();
}

void VSThreadPool::setMaxThreadCount(int threadCount) {
    QMutexLocker m(&lock);

    while (threadCount > allThreads.count()) {
        VSThread *vst = new VSThread(this);
        allThreads.insert(vst);
        vst->start();
    }

    while (threadCount < allThreads.count()) {
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
}

void VSThreadPool::wakeThread() {
    if (activeThreads < threadCount())
        newWork.wakeOne();
}

void VSThreadPool::releaseThread() {
    setMaxThreadCount(allThreads.count() + 1);
}

void VSThreadPool::reserveThread() {
    setMaxThreadCount(allThreads.count() - 1);
}

void VSThreadPool::notifyCaches(CacheActivation reason) {
    for (int i = 0; i < core->caches.count(); i++)
        tasks.append(PFrameContext(new FrameContext(reason, core->caches[i], PFrameContext())));
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
    QMutexLocker m(&callbackLock);
    rCtx->frameDone(rCtx->userData, ref, rCtx->n, rCtx->node, NULL);
    m.unlock();
    lock.lock();
}

void VSThreadPool::startInternal(const PFrameContext &context) {
    //technically this could be done by walking up the context chain and add a new notification to the correct one
    //unfortunately this would probably be quite slow for deep scripts so just hope the cache catches it

    if (context->n < 0)
        qFatal("Negative frame request by: " + context->clip->name);

    // check to see if it's time to reevaluate cache sizes
    if (!context->upstreamContext && ticks.fetchAndAddAcquire(1) == 99) {
        ticks = 0;
        notifyCaches(cCacheTick);
    }

    // add it immediately if the task is to return a completed frame
    if (context->returnedFrame) {
        tasks.append(context);
        wakeThread();
        return;
    } else {
        if (context->upstreamContext)
            context->upstreamContext->numFrameRequests.ref();

        ////////////////////////
        // see if the task is a duplicate
        foreach(const PFrameContext & ctx, tasks) {
            if (context->clip == ctx->clip && context->n == ctx->n) {
                if (ctx->returnedFrame) {
                    // special case where the requested frame is encountered "by accident"
                    context->returnedFrame = ctx->returnedFrame;
                    tasks.append(context);
                    wakeThread();
                    return;
                } else {
                    PFrameContext rCtx = ctx;

                    if (rCtx->returnedFrame)
                        rCtx = rCtx->upstreamContext;

                    if (context->clip == rCtx->clip && context->n == rCtx->n) {
                        PFrameContext t = rCtx;

                        while (t && t->notificationChain)
                            t = t->notificationChain;

                        t->notificationChain = context;
                        return;
                    }
                }
            }
        }

        FrameKey p(context->clip, context->n);

        if (runningTasks.contains(p)) {
            PFrameContext ctx = runningTasks[p];
            Q_ASSERT(ctx);

            if (ctx->returnedFrame) {
                // special case where the requested frame is encountered "by accident"
                context->returnedFrame = ctx->returnedFrame;
                tasks.append(context);
                wakeThread();
                return;
            } else {
                PFrameContext rCtx = ctx;

                if (rCtx->returnedFrame)
                    rCtx = rCtx->upstreamContext;

                if (context->clip == rCtx->clip && context->n == rCtx->n) {
                    PFrameContext t = rCtx;

                    while (t && t->notificationChain)
                        t = t->notificationChain;

                    t->notificationChain = context;
                    return;
                }
            }
        }

        if (allContexts.contains(p)) {
            PFrameContext ctx = allContexts[p];
            Q_ASSERT(ctx);
            Q_ASSERT(context->clip == ctx->clip && context->n == ctx->n);

            if (ctx->returnedFrame) {
                // special case where the requested frame is encountered "by accident"
                context->returnedFrame = ctx->returnedFrame;
                tasks.append(context);
                wakeThread();
                return;
            } else {
                while (ctx->notificationChain)
                    ctx = ctx->notificationChain;

                ctx->notificationChain = context;
                return;
            }
        } else {
            allContexts[p] = context;
        }

        tasks.append(context);
        wakeThread();
        return;
    }

}

void VSThreadPool::waitForDone() {

}

VSThreadPool::~VSThreadPool() {
    setMaxThreadCount(0);
};
