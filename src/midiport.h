#ifndef VC_MIDIPORT_H
#define VC_MIDIPORT_H
/* Virtual MIDI output port ("Velvet Chords") through the ALSA sequencer, which MPC OS (Linux) uses for MIDI.
 * libasound is loaded at runtime, so the plugin still loads where it's missing; open() then just fails. */
typedef struct {
    void *lib;
    void *seq;
    int port, client;
    char status[96];
    void *out_fn, *close_fn;   /* the ALSA entry points, looked up when this port opens (per port, not shared) */
} MidiPort;

int vc_port_open(MidiPort *m, const char *client_name, const char *port_name); /* 1 = ok */
void vc_port_send(MidiPort *m, const unsigned char *msg);                     /* 3-byte channel message */
void vc_port_close(MidiPort *m);
int vc_port_is_open(const MidiPort *m);
#endif
