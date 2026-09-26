/* The control for L24/L25: dlopen and dlclose several libraries from eight threads, with no Vulkan
   call anywhere.

   L24 read ~800 ThreadSanitizer reports per ten seconds -- one thread freeing a driver library
   inside `_dl_close_worker` while another read it -- as a defect in the Vulkan loader. This says
   otherwise. Point it at the same ICD libraries the loader opens and it produces the same reports,
   with the same `_dl_close_worker` + rtld-malloc frames, out of nothing but the dynamic linker:

     cc -O2 -std=gnu11 -o rtld_dlrace rtld_dlrace.c -ldl -lpthread
     LD_PRELOAD=$(gcc -print-file-name=libtsan.so) TSAN_OPTIONS=handle_segv=0 \
       ./rtld_dlrace $(python3 -c "import json,glob,sys
       print(' '.join(json.load(open(f))['ICD']['library_path'] for f in sorted(glob.glob(sys.argv[1]))))" \
       '/usr/share/vulkan/icd.d/[a-z]*.json')

   glibc runs `_dl_close_worker` under `GL(dl_load_lock)` (elf/dl-close.c: the lock is taken in
   `_dl_close` and released after the worker returns), and that lock is an `__rtld_lock_*`, not a
   pthread mutex ThreadSanitizer intercepts. So TSan sees two threads touching the link map with no
   happens-before it can observe, and reports a race that is not one. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static volatile int stop;
static char **libs; static int nlibs;
static void *worker(void *p) {
    unsigned seed = (unsigned)(size_t)p; long n = 0;
    while (!stop) {
        void *h[16]; int k, count = nlibs < 16 ? nlibs : 16;
        for (k = 0; k < count; k++) h[k] = dlopen(libs[(seed + k) % nlibs], RTLD_NOW | RTLD_LOCAL);
        for (k = 0; k < count; k++) if (h[k]) dlclose(h[k]);
        seed = seed * 1664525u + 1013904223u;
        n++;
    }
    return (void *)(size_t)n;
}
int main(int argc, char **argv) {
    pthread_t t[8]; int i; long total = 0;
    struct timespec nap = { 10, 0 };
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { printf("usage: dlrace2 <lib> [lib...]\n"); return 3; }
    libs = argv + 1; nlibs = argc - 1;
    printf("dlopen/dlclose over %d libraries, 8 threads, 10 s\n", nlibs);
    for (i = 0; i < 8; i++) pthread_create(&t[i], NULL, worker, (void *)(size_t)(i + 1));
    nanosleep(&nap, NULL);
    stop = 1;
    for (i = 0; i < 8; i++) { void *r; pthread_join(t[i], &r); total += (long)(size_t)r; }
    printf("  %ld rounds\n", total);
    return 0;
}
