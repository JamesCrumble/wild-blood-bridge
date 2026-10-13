#pragma once

/*
 * What the parts of this port need from each other.
 *
 * Nothing generic belongs here - portbase owns the loader, the thunks, the
 * fake JVM and the input interface. This is only the handful of facts that are
 * about Wild Blood specifically and are needed in more than one file.
 */

struct so_module;
struct _JNIEnv;

/* The loaded guest library. Same pointer port_guest_module() returns; declared
 * here so the JNI classes can forward to the game's own native methods without
 * threading it through the class registry. */
so_module *wb_module(void);

/*
 * The entry points this port drives are spread over three Java classes of the
 * game's package, and the mangled names are long enough that spelling them out
 * at each call site is how a typo becomes a null function pointer. so_symbol()
 * answers NULL for a name that does not exist and says nothing about it.
 */
#define WB_JNI(cls, m) "Java_com_gameloft_android_ANMP_GloftWBHM_" cls "_" m

/* Storage for the four GDRM lock words Game.nativeInit() writes through. patch.cpp */
void wb_unlock_drm(so_module *mod);

/* The module's own copies of fopen & co. are sent to the loader's. patch.cpp */
void wb_bridge_private_stdio(so_module *mod);

/* Engine settings forced over the device profile. tweaks.cpp */
void wb_tweaks_install(so_module *mod);
/* Once per frame: the scheduler's clamp on the main thread. tweaks.cpp */
void wb_sched_tick(long frames);

/*
 * Create the directories under the writable storage that the engine writes
 * into without ever creating them. Called from main() before the boot
 * sequence; implemented next to the path rules in game/io_paths.cpp.
 */
void wb_prepare_writable_storage(void);

/* The directory the engine reads its data from: <game>/files (AOWGame/,
 * Engine/ under it), which is what /sdcard/Android/data/<package>/files maps to. This is what
 * Game.getSDFolder() answers. */
const char *wb_resource_dir(void);

/*
 * Game.EglSwapR(): the engine asks for a frame to be shown right now, from
 * inside a nativeRender (loading screens). The ordinary frame is shown by
 * main.cpp after each nativeRender; this is the extra one.
 */
void wb_present(void);

/* Tell the engine a movie it asked Java to play has ended
 * (MyVideoView.videoFinished). main.cpp */
void wb_movie_finished(_JNIEnv *env);
