/* Preloaded into the host test to count heap calls made while the plugin processes audio or MIDI.
 * The test turns vc_probe_on on around processReplacing and effProcessEvents; any malloc, calloc, realloc or free
 * in between is counted in vc_probe_count. Nothing is counted otherwise, and nothing changes about the calls. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

volatile int vc_probe_on;
volatile long vc_probe_count;

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void (*real_free)(void *);
static char boot[8192];   /* calloc during dlsym itself, before the real functions are known */
static size_t boot_used;
static int initialising;

static void init(void) {
    initialising = 1;
    real_malloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "malloc");
    real_calloc = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "calloc");
    real_realloc = (void *(*)(void *, size_t))dlsym(RTLD_NEXT, "realloc");
    real_free = (void (*)(void *))dlsym(RTLD_NEXT, "free");
    initialising = 0;
}

/* VC_PROBE_TRACE=1 prints where the first few counted calls came from */
static void note_call(void) {
    vc_probe_count++;
    if (vc_probe_count <= 5 && getenv("VC_PROBE_TRACE")) {
        vc_probe_on = 0;
        void *bt[16];
        int n = backtrace(bt, 16);
        backtrace_symbols_fd(bt, n, 2);
        vc_probe_on = 1;
    }
}

static int in_boot(void *p) { return (char *)p >= boot && (char *)p < boot + sizeof(boot); }

void *malloc(size_t n) {
    if (!real_malloc && !initialising) init();
    if (initialising) { void *p = boot + boot_used; boot_used += (n + 15) & ~(size_t)15; return boot_used <= sizeof(boot) ? p : 0; }
    if (vc_probe_on) note_call();
    return real_malloc(n);
}

void *calloc(size_t a, size_t b) {
    if (!real_calloc && !initialising) init();
    if (initialising) { size_t n = (a * b + 15) & ~(size_t)15; void *p = boot + boot_used; boot_used += n; if (boot_used > sizeof(boot)) return 0; memset(p, 0, a * b); return p; }
    if (vc_probe_on) note_call();
    return real_calloc(a, b);
}

void *realloc(void *p, size_t n) {
    if (!real_realloc && !initialising) init();
    if (vc_probe_on) note_call();
    if (in_boot(p)) { void *q = real_malloc(n); if (q && p) memcpy(q, p, n); return q; }
    return real_realloc(p, n);
}

void free(void *p) {
    if (!p || in_boot(p)) return;
    if (!real_free && !initialising) init();
    if (vc_probe_on) note_call();
    real_free(p);
}
