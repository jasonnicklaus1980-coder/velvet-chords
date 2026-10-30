#include "mpc_list.h"
#include "xml_min.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mpc_walk_plugins(const char *dir, char paths[][VH_PATH_LEN], int max, int depth) {
    if (!dir || !paths || max <= 0) return -1;
    DIR *d = opendir(dir);
    if (!d) return -1;
    int count = 0;
    struct dirent *de;
    while (count < max && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[VH_PATH_LEN];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        size_t len = strlen(de->d_name);
        if (len >= 4 && strcmp(de->d_name + len - 3, ".so") == 0) {
            snprintf(paths[count++], VH_PATH_LEN, "%s", path);
            continue;
        }
        /* not a plugin: search it if it's a folder (opendir succeeds only on folders; stat() would need a newer glibc) */
        if (depth > 0) {
            DIR *sub = opendir(path);
            if (sub) {
                closedir(sub);
                int got = mpc_walk_plugins(path, paths + count, max - count, depth - 1);
                if (got > 0) count += got;
            }
        }
    }
    closedir(d);
    return count;
}

#define MPC_SETTINGS_DIR "/media/az01-internal/Settings"
#define VH_OWN_UID_HEX 0x566c7643UL   /* 'VlvC', Velvet Chords' own unique ID: never listed as a hosted plugin */

int mpc_settings_find(char *buf, size_t len) {
    const char *env = getenv("VC_SETTINGS_FILE");
    if (env && *env) {
        snprintf(buf, len, "%s", env);
        return 0;
    }
    DIR *d = opendir(MPC_SETTINGS_DIR);
    if (!d) return -1;
    int found = -1;
    struct dirent *de;
    while (found != 0 && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        snprintf(buf, len, "%s/%s/MPC.settings", MPC_SETTINGS_DIR, de->d_name);
        FILE *f = fopen(buf, "rb");
        if (f) {
            fclose(f);
            found = 0;
        }
    }
    closedir(d);
    return found;
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    char *b = malloc(cap);
    while (b) {
        if (n == cap) {
            char *nb = realloc(b, cap * 2);
            if (!nb) { free(b); b = NULL; break; }
            b = nb;
            cap *= 2;
        }
        size_t got = fread(b + n, 1, cap - n, f);
        if (got == 0) break;
        n += got;
    }
    fclose(f);
    if (b) *len = n;
    return b;
}

/* depth-first search for the element with this name and a name= attribute */
static const XNode *find_named(const XNode *n, const char *elem, const char *name) {
    for (const XNode *c = n; c; c = c->next) {
        if (!strcmp(c->name, elem)) {
            const char *a = xml_attr(c, "name");
            if (a && !strcmp(a, name)) return c;
        }
        const XNode *hit = find_named(c->child, elem, name);
        if (hit) return hit;
    }
    return NULL;
}

int mpc_plugin_list(const char *settings_path, MpcPlugin *out, int max) {
    if (!settings_path || !out || max <= 0) return -1;
    size_t len = 0;
    char *text = read_file(settings_path, &len);
    if (!text) return -1;
    XNode *root = xml_parse(text, len);
    free(text);
    if (!root) return -1;

    int count = 0;
    const XNode *value = find_named(root, "VALUE", "pluginList-arm");
    const XNode *known = value ? xml_child(value, "KNOWNPLUGINS") : NULL;
    for (const XNode *p = known ? known->child : NULL; p && count < max; p = p->next) {
        if (strcmp(p->name, "PLUGIN") != 0) continue;
        const char *inst = xml_attr(p, "isInstrument");
        const char *file = xml_attr(p, "file");
        const char *uid = xml_attr(p, "uid");
        if (!inst || strcmp(inst, "1") != 0 || !file || !*file) continue;
        if (uid && strtoul(uid, NULL, 16) == VH_OWN_UID_HEX) continue;
        MpcPlugin *m = &out[count++];
        snprintf(m->path, sizeof m->path, "%s", file);
        snprintf(m->name, sizeof m->name, "%s", xml_attr(p, "name") ? xml_attr(p, "name") : file);
    }
    xml_free(root);
    return count;
}
