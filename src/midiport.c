#include "midiport.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <string.h>

/* ALSA sequencer ABI (alsa-lib, stable since 0.9). Declared here so no ALSA headers are needed to build. */
typedef struct {
    unsigned char type, flags, tag, queue;
    unsigned int time[2];
    unsigned char src_client, src_port, dst_client, dst_port;
    union {
        struct { unsigned char channel, note, velocity, off_velocity; unsigned int duration; } note;
        struct { unsigned char channel, unused[3]; unsigned int param; int value; } ctrl;
        unsigned char raw8[12];
    } data;
} SeqEvent; /* == snd_seq_event_t, 28 bytes */

typedef char check_seq_event_size[sizeof(SeqEvent) == 28 ? 1 : -1];

enum { SEQ_OPEN_OUTPUT = 1, SEQ_NONBLOCK = 1 };
enum { CAP_READ = 1 << 0, CAP_SUBS_READ = 1 << 5 };
enum { TYPE_MIDI_GENERIC = 1 << 1, TYPE_SOFTWARE = 1 << 16, TYPE_APPLICATION = 1 << 20 };
enum { EV_NOTEON = 6, EV_NOTEOFF = 7, EV_CONTROLLER = 10, EV_PITCHBEND = 13 };
enum { QUEUE_DIRECT = 253, ADDR_SUBSCRIBERS = 254, ADDR_UNKNOWN = 253 };

typedef int (*open_fn)(void **, const char *, int, int);
typedef int (*close_fn)(void *);
typedef int (*name_fn)(void *, const char *);
typedef int (*port_fn)(void *, const char *, unsigned int, unsigned int);
typedef int (*out_fn)(void *, SeqEvent *);
typedef int (*id_fn)(void *);
typedef const char *(*err_fn)(int);


int vc_port_is_open(const MidiPort *m) { return m && m->seq != 0; }

#ifdef _WIN32
/* the virtual port is ALSA, which Windows doesn't have: the plugin still plays its own sound */
int vc_port_open(MidiPort *m, const char *client_name, const char *port_name) {
    (void)m; (void)client_name; (void)port_name;
    snprintf(m->status, sizeof(m->status), "Virtual MIDI port is not available on Windows");
    return 0;
}
#else
int vc_port_open(MidiPort *m, const char *client_name, const char *port_name) {
    if (m->seq) return 1;
    if (!m->lib) m->lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!m->lib) { snprintf(m->status, sizeof(m->status), "No ALSA library on this system"); return 0; }
    open_fn o = (open_fn)dlsym(m->lib, "snd_seq_open");
    name_fn nm = (name_fn)dlsym(m->lib, "snd_seq_set_client_name");
    port_fn pt = (port_fn)dlsym(m->lib, "snd_seq_create_simple_port");
    id_fn idf = (id_fn)dlsym(m->lib, "snd_seq_client_id");
    err_fn er = (err_fn)dlsym(m->lib, "snd_strerror");
    m->out_fn = (void *)dlsym(m->lib, "snd_seq_event_output_direct");
    m->close_fn = (void *)dlsym(m->lib, "snd_seq_close");
    if (!o || !nm || !pt || !idf || !m->out_fn || !m->close_fn) { snprintf(m->status, sizeof(m->status), "ALSA library incomplete"); return 0; }
    void *seq = 0;
    int r = o(&seq, "default", SEQ_OPEN_OUTPUT, SEQ_NONBLOCK);
    if (r < 0 || !seq) { snprintf(m->status, sizeof(m->status), "MIDI port unavailable: %s", er ? er(r) : "error"); return 0; }
    nm(seq, client_name);
    int port = pt(seq, port_name, CAP_READ | CAP_SUBS_READ, TYPE_MIDI_GENERIC | TYPE_SOFTWARE | TYPE_APPLICATION);
    if (port < 0) {
        snprintf(m->status, sizeof(m->status), "Couldn't create port: %s", er ? er(port) : "error");
        ((close_fn)m->close_fn)(seq);
        return 0;
    }
    m->seq = seq;
    m->port = port;
    m->client = idf(seq);
    snprintf(m->status, sizeof(m->status), "Port open: %s (%d:%d)", port_name, m->client, m->port);
    return 1;
}
#endif

void vc_port_send(MidiPort *m, const unsigned char *msg) {
    if (!m->seq || !m->out_fn) return;
    SeqEvent ev;
    memset(&ev, 0, sizeof(ev));
    int st = msg[0] & 0xF0, ch = msg[0] & 0x0F;
    if (st == 0x90 && msg[2] > 0) { ev.type = EV_NOTEON; ev.data.note.channel = (unsigned char)ch; ev.data.note.note = msg[1]; ev.data.note.velocity = msg[2]; }
    else if (st == 0x80 || st == 0x90) { ev.type = EV_NOTEOFF; ev.data.note.channel = (unsigned char)ch; ev.data.note.note = msg[1]; }
    else if (st == 0xB0) { ev.type = EV_CONTROLLER; ev.data.ctrl.channel = (unsigned char)ch; ev.data.ctrl.param = msg[1]; ev.data.ctrl.value = msg[2]; }
    else return;
    ev.queue = QUEUE_DIRECT;              /* deliver now */
    ev.src_port = (unsigned char)m->port;
    ev.dst_client = ADDR_SUBSCRIBERS;     /* to whoever listens (MPC tracks set to this input) */
    ev.dst_port = ADDR_UNKNOWN;
    ((out_fn)m->out_fn)(m->seq, &ev);                   /* non-blocking; a full buffer drops the event rather than stalling audio */
}

void vc_port_close(MidiPort *m) {
    if (m->seq && m->close_fn) ((close_fn)m->close_fn)(m->seq);
    m->seq = 0;
    snprintf(m->status, sizeof(m->status), "Port closed");
}
