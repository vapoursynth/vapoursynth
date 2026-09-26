/* The GPU probes were written against the handful of Win32 threading calls available on the
   development machine. Rather than fork them for Linux, this maps exactly those calls onto
   pthreads and the compiler's atomic builtins, so one source builds and runs on both and the
   two machines' results are directly comparable.

   Deliberately small: only what the probes actually use. If a probe needs something else,
   add it here rather than adding a platform #ifdef to the probe. */
#ifndef PROBE_COMPAT_H
#define PROBE_COMPAT_H

#ifdef _WIN32

#include <windows.h>
#define probe_setenv(name, value) _putenv_s((name), (value))

static inline unsigned long long probe_millis(void) { return (unsigned long long)GetTickCount64(); }

/* Atomic load of a counter the Interlocked* helpers write. Reading one plainly while another
   thread increments it is a data race even when the value only drives progress reporting, and
   TSan reports it -- which buries real findings. */
static inline LONG InterlockedGet(volatile LONG *value) {
    return InterlockedCompareExchange(value, 0, 0);
}

#else

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WINAPI
typedef void *LPVOID;
typedef unsigned long DWORD;
typedef long LONG;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFFu
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0u
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 258u
#endif

typedef DWORD (WINAPI *ProbeThreadFn)(LPVOID);

typedef struct ProbeThread {
    pthread_t tid;
    ProbeThreadFn fn;
    void *arg;
    int joined;
} ProbeThread;

typedef ProbeThread *HANDLE;

static void *probe_thread_entry(void *p) {
    ProbeThread *t = (ProbeThread *)p;
    t->fn(t->arg);
    return NULL;
}

static inline HANDLE CreateThread(void *attrs, size_t stack, ProbeThreadFn fn, void *arg,
    unsigned flags, unsigned long *idOut) {
    ProbeThread *t;
    (void)attrs; (void)stack; (void)flags;
    if (idOut)
        *idOut = 0;
    t = (ProbeThread *)malloc(sizeof(*t));
    if (!t)
        return NULL;
    t->fn = fn;
    t->arg = arg;
    t->joined = 0;
    if (pthread_create(&t->tid, NULL, probe_thread_entry, t) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

/* Waits for all of them, honouring the timeout. gpu_stress's watchdog calls this in a loop with
   500 ms and reads WAIT_TIMEOUT as "still running", so joining unconditionally would turn the
   hang it is trying to report into a real one. Each handle is joined at most once; joining an
   already-joined thread is undefined. */
static inline unsigned WaitForMultipleObjects(unsigned count, HANDLE *handles, int waitAll,
    unsigned timeoutMs) {
    unsigned i;
    struct timespec deadline;
    (void)waitAll;

    if (timeoutMs != INFINITE) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += (time_t)(timeoutMs / 1000u);
        deadline.tv_nsec += (long)(timeoutMs % 1000u) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    for (i = 0; i < count; i++) {
        if (!handles[i] || handles[i]->joined)
            continue;
        if (timeoutMs == INFINITE) {
            pthread_join(handles[i]->tid, NULL);
        } else if (pthread_timedjoin_np(handles[i]->tid, NULL, &deadline) != 0) {
            return WAIT_TIMEOUT;
        }
        handles[i]->joined = 1;
    }
    return WAIT_OBJECT_0;
}

/* Detaches a thread that was never waited for, so the handle does not leak its stack. */
static inline void CloseHandle(HANDLE h) {
    if (!h)
        return;
    if (!h->joined)
        pthread_detach(h->tid);
    free(h);
}

static inline void Sleep(unsigned ms) {
    usleep((useconds_t)ms * 1000u);
}

static inline LONG InterlockedIncrement(volatile LONG *value) {
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedExchange(volatile LONG *target, LONG value) {
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

/* Atomic load of a counter the Interlocked* helpers write. Reading one plainly while another
   thread increments it is a data race even when the value only drives progress reporting, and
   TSan reports it -- which buries real findings. */
static inline LONG InterlockedGet(volatile LONG *value) {
    return __atomic_load_n(value, __ATOMIC_SEQ_CST);
}

/* The watchdogs kill the process rather than let a hang look like a slow test. */
static inline void *GetCurrentProcess(void) { return NULL; }
static inline void TerminateProcess(void *proc, int code) { (void)proc; _exit(code); }

#include <time.h>
static inline unsigned long long probe_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)(ts.tv_nsec / 1000000);
}

#define probe_setenv(name, value) setenv((name), (value), 1)

#endif /* _WIN32 */

/* How much memory the host can spare right now, in MB, or 0 when it cannot be determined.
   Used to cap allocation probes: on unified memory a DEVICE_LOCAL allocation comes out of
   system RAM, so an uncapped "allocate until refused" loop takes the desktop down instead of
   reaching the allocator's failure rung. */
#ifdef _WIN32
static inline long long probe_avail_mem_mb(void) {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return 0;
    return (long long)(ms.ullAvailPhys >> 20);
}
#else
static inline long long probe_avail_mem_mb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    char line[256];
    long long kb = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemAvailable: %lld kB", &kb) == 1)
            break;
        kb = 0;
    }
    fclose(f);
    return kb / 1024;
}
#endif

/* VSVulkanCoreInfo carries the flag under different names across revisions; keep the probes
   free of that detail. */
#define probe_unified_memory(ci) ((int)((ci)->unifiedMemory))

#endif /* PROBE_COMPAT_H */

/* A lock a probe may use for its own shared state. Not the Interlocked* helpers: the probes are
   built without instrumentation and run against the instrumented core, so an atomic spin lock of
   their own carries no happens-before ThreadSanitizer can see, and every core object handed from
   one probe thread to another through it is reported as a race with its own allocation ("as if
   synchronized via sleep"). pthread_mutex_* are intercepted whoever calls them. */
#ifdef _WIN32
typedef CRITICAL_SECTION probe_mutex_t;
static inline void probe_mutex_init(probe_mutex_t *m) { InitializeCriticalSection(m); }
static inline void probe_mutex_lock(probe_mutex_t *m) { EnterCriticalSection(m); }
static inline void probe_mutex_unlock(probe_mutex_t *m) { LeaveCriticalSection(m); }
#else
#include <pthread.h>
typedef pthread_mutex_t probe_mutex_t;
static inline void probe_mutex_init(probe_mutex_t *m) { pthread_mutex_init(m, NULL); }
static inline void probe_mutex_lock(probe_mutex_t *m) { pthread_mutex_lock(m); }
static inline void probe_mutex_unlock(probe_mutex_t *m) { pthread_mutex_unlock(m); }
#endif
