/*
 * Path translation for Wild Blood (1.1.5, GloftWBHM, Unreal Engine 3).
 *
 * See portbase/src/io_paths.h for the split between what portbase answers and
 * what only this game knows. The names below are in the library's .rodata
 * (strings libUnrealEngine3.so):
 *
 *   /sdcard/gameloft/games/GloftWBHM
 *   /data/data/com.gameloft.android.ANMP.GloftWBHM
 *
 * The engine builds every name under the second one, whatever Java says the
 * SD folder is, so the rewrite rules here are what points it at the cache.
 *
 * The game data is a tree of flat files (AOWGame/CookedAndroid_ETC/*.xxx,
 * AOWGame/Audio, Engine/), the "cache" that goes to
 * /sdcard/Android/data/<package>/files on a phone. Here it is <game>/files. What the game writes (saves, preferences) goes to
 * io_writable_dir().
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "io_paths.h"
#include "so_util.h"
#include "trace.h"

#include "wb.h"

/* The package, in the spellings the binary and its Java use. */
static const char kDataData[] = "/data/data/com.gameloft.android.ANMP.GloftWBHM";
static const char kExtFiles[] = "/sdcard/Android/data/com.gameloft.android.ANMP.GloftWBHM/files";
static const char kSdFolder[] = "/sdcard/gameloft/games/GloftWBHM";

const char *wb_resource_dir(void)
{
    static char dir[PATH_MAX];
    if (!dir[0])
        snprintf(dir, sizeof(dir), "%s/files", io_game_dir());
    return dir;
}

/*
 * Re-root `path` at `root`, keeping whatever followed `prefix`.
 *
 * The tail can be empty (the game asks for the bare directory when it builds
 * its own paths by concatenation) and it can start with or without a slash, so
 * the separator is normalised here rather than at each call site.
 */
static const char *reroot(const char *path, const char *prefix, const char *root,
                          char *buf, size_t bufsz)
{
    const char *tail = path + strlen(prefix);
    while (*tail == '/')
        tail++;

    if (*tail)
        snprintf(buf, bufsz, "%s/%s", root, tail);
    else
        snprintf(buf, bufsz, "%s", root);

    return buf;
}

static bool has_prefix(const char *path, const char *prefix)
{
    size_t n = strlen(prefix);
    return strncmp(path, prefix, n) == 0 && (path[n] == '/' || path[n] == '\0');
}

const char *port_fix_path(const char *orig, char *buf, size_t bufsz)
{
    if (!orig || !*orig)
        return NULL;

    /*
     * A path this port handed the engine can come back with a "." glued to
     * the front, which makes it a relative name. A leading "./" in
     * front of one of the roots this port knows can only have come from that
     * concatenation, so the dot is dropped - and only then: "./data/foo" is a
     * relative name the engine means literally.
     */
    bool undotted_here = false;

    if (orig[0] == '.' && orig[1] == '/') {
        const char *undotted = orig + 1;
        const char *roots[] = {
            io_game_dir(), io_writable_dir(), wb_resource_dir(),
            kExtFiles, kSdFolder, kDataData,
        };

        for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
            if (roots[i] && *roots[i] && has_prefix(undotted, roots[i])) {
                orig = undotted;
                undotted_here = true;
                break;
            }
        }
    }

    if (has_prefix(orig, kExtFiles))
        return reroot(orig, kExtFiles, wb_resource_dir(), buf, bufsz);

    if (has_prefix(orig, kSdFolder))
        return reroot(orig, kSdFolder, wb_resource_dir(), buf, bufsz);

    /* The engine's own root. It never asks Java where the cache is: every
     * package, config and save name is built under /data/data/<package>
     * (AOWGame/CookedAndroid_ETC/..., AOWGame/Logs, *.sav). So that whole tree
     * is the cache directory, and what the game writes lands beside its data. */
    if (has_prefix(orig, kDataData))
        return reroot(orig, kDataData, wb_resource_dir(), buf, bufsz);

    /* An undotted host path is already the answer; returning NULL would give
     * portbase the dotted original and it would resolve it against the game
     * directory again. */
    if (undotted_here) {
        snprintf(buf, bufsz, "%s", orig);
        return buf;
    }

    /* No rule. NULL, not `orig`: portbase's generic fallbacks resolve the bare
     * relative names against the game tree. */
    return NULL;
}

/* The fonts the game uses are cooked into its own packages. */
const char *port_system_font(void)
{
    return NULL;
}

void wb_prepare_writable_storage(void)
{
    /* Nothing to create up front: the engine makes its own directories under
     * the data folder (mkdir is one of its imports). */
}
