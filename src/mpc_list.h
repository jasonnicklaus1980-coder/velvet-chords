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

/* The MPC's expansions folder, as given by the user (not yet checked on a device). The VC_EXPANSIONS_DIR environment
 * variable overrides it (test hook). */
#define MPC_EXPANSIONS_DIR "/media/az01-internal/Expansions"
#define MPC_EXPANSIONS_SSD_DIR "/media/SSD/Expansions"   /* the expansions on the SSD, as found on the MPC */
#define MPC_WALK_DEPTH 4   /* how many subfolder levels below each plugin folder are searched */

/* Writes the path of every .so file in dir and its subfolders (up to depth levels down) into paths, up to max.
 * Returns the count, or -1 if dir can't be read. Nothing is loaded. */
int mpc_walk_plugins(const char *dir, char paths[][VH_PATH_LEN], int max, int depth);

/* The same walk for any file extension (case-insensitive, such as ".xpm"). */
int mpc_walk_files(const char *dir, char paths[][VH_PATH_LEN], int max, int depth, const char *ext);

#endif
