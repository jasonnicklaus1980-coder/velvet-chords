/* Reads the plugin list the MPC keeps in MPC.settings (<VALUE name="pluginList-arm"><KNOWNPLUGINS>), so Velvet Chords
 * can cycle through the same instruments the MPC shows. Plain C; libc and src/xml_min only. */
#ifndef VC_MPC_LIST_H
#define VC_MPC_LIST_H
#include <stddef.h>
#include "vhost.h"

typedef struct {
    char path[VH_PATH_LEN];   /* file= from the list entry, e.g. /sdcard/vst/some.so */
    char name[VH_NAME_LEN];   /* name= from the list entry */
} MpcPlugin;

/* Writes the path of MPC.settings to buf. Uses the VC_SETTINGS_FILE environment variable when it is set (test hook),
 * otherwise the first Settings/<folder>/MPC.settings on the internal drive. Returns 0 if a file was found, -1 if not. */
int mpc_settings_find(char *buf, size_t len);

/* Reads the instruments (isInstrument="1") in the plugin list of settings_path, in list order, up to max entries.
 * Velvet Chords itself is left out. Returns the count (0 if the file has no plugin list), or -1 if the file can't be read
 * or isn't well-formed XML. */
int mpc_plugin_list(const char *settings_path, MpcPlugin *out, int max);

#endif
