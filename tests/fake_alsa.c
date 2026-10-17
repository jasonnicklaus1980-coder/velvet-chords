/* Stand-in for libasound.so.2 used only in tests: records the sequencer calls the plugin makes.
 * Log lines: "open name streams mode", "client NAME", "port NAME caps type", "ev type ch note vel queue dstc dstp srcp", "close". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *alog(void) {
    static FILE *f;
    if (!f) { const char *p = getenv("VC_FAKE_ALSA_LOG"); f = fopen(p ? p : "/tmp/fake_alsa.log", "w"); setvbuf(f, 0, _IOLBF, 0); }
    return f;
}
typedef struct { unsigned char type, flags, tag, queue; unsigned int time[2]; unsigned char sc, sp, dc, dp; unsigned char d[12]; } Ev;
static int dummy;
int snd_seq_open(void **h, const char *name, int streams, int mode) { *h = &dummy; fprintf(alog(), "open %s %d %d\n", name, streams, mode); return 0; }
int snd_seq_set_client_name(void *h, const char *n) { fprintf(alog(), "client %s\n", n); return 0; }
int snd_seq_create_simple_port(void *h, const char *n, unsigned caps, unsigned type) { fprintf(alog(), "port %s %u %u\n", n, caps, type); return 0; }
int snd_seq_client_id(void *h) { return 129; }
int snd_seq_event_output_direct(void *h, Ev *e) {
    fprintf(alog(), "ev %d %d %d %d %d %d %d %d %d\n", e->type, e->d[0], e->d[1], e->d[2], e->queue, e->dc, e->dp, e->sp, (int)sizeof(Ev));
    return 0;
}
int snd_seq_close(void *h) { fprintf(alog(), "close\n"); return 0; }
const char *snd_strerror(int e) { return "fake"; }
