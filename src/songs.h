#ifndef VC_SONGS_H
#define VC_SONGS_H

typedef struct { const char *chord; float beats; } SongEvent;
typedef struct { const char *name; const SongEvent *ev; int nev; } SongSection;
typedef struct {
    const char *id, *title, *artist, *writers;
    int year;
    const char *key;
    int key_root, minor;
    const char *genre, *status, *confidence, *recording;
    float bpm;          /* 0 = not verified */
    const char *timesig; /* NULL = not verified */
    int ts_num, ts_den, ts_verified;
    const char *notes;
    const SongSection *sections;
    int nsections;
} Song;

extern const Song VC_SONGS[];
extern const int VC_NUM_SONGS;

#endif
