/* The song chord charts were removed: Velvet Chords ships no song data. Song mode stays in the code for saved projects
 * (it has no songs, so it shows nothing). One blank entry keeps the table non-empty for C; VC_NUM_SONGS is the count. */
#include <stddef.h>
#include "songs.h"

const Song VC_SONGS[1] = {{"", "", "", "", 0, "", 0, 0, "", "", "", "", 0.f, NULL, 0, 0, 0, "", NULL, 0}};
const int VC_NUM_SONGS = 0;
