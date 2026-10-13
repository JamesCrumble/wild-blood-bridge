/*
 * Wild Blood - entry point.
 *
 * This game is driven, not hosted. It declares no libandroid.so, exports no
 * ANativeActivity_onCreate, and has no frame loop of its own: what it exports
 * is JNI_OnLoad plus a wall of Java_* entry points, and on Android a Java layer
 * calls them one by one. Here that Java layer is this file.
 *
 *   $ readelf -d libUnrealEngine3.so | grep NEEDED
 *   libEGL, liblog, libGLESv2, libz, libstdc++, libm, libc, libdl, libjnigraphics
 *
 * The library is Unreal Engine 3 (SONAME libAOWGame-Android-Release.so) under
 * Gameloft's own Android glue, not Epic's: there is no engine thread started
 * from NativeCallback_Init. GLESv2 only, audio through a Java AudioTrack (vox).
 *
 * The call order below is the one the game's own Java performs
 * (GameRenderer.b() and GameRenderer.e() in classes.dex):
 *
 *   Game.nativeInit()                       looks up every Java callback
 *   GameRenderer.nativeInit(manufacturer, width, height, versionName)
 *   GameRenderer.nativeResize(width, height)
 *   GameRenderer.nativeRender()             once per frame
 *
 * nativeRender is appUpdate(): its first call runs appInit() (the engine's
 * start-up, continued over the next frames), every later one is
 * FEngineLoop::Tick(). The frame is shown by the caller after each
 * nativeRender, as the GLSurfaceView does on Android; see the loop.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <SDL2/SDL.h>

#include "port_version.h"
#include "so_util.h"
#include "khronos/gles2.h"

#include "jni.h"

#include "app_exit.h"
#include "crash.h"
#include "cursor_draw.h"
#include "emulator_control.h"
#include "fb_probe.h"
#include "fix_path.h"
#include "input_bridge.h"
/* io_set_game_dir/io_game_dir come from fix_path.h and NOT from io_paths.h.
 * The two headers declare the same three functions with different linkage -
 * io_paths.h wraps them in extern "C" - so including both is a compile error.
 * Noted for portbase; here the loader-side header is the right one. */
#include "patch.h"
#include "port_env.h"
#include "trace.h"
#include "gl_bridge.h"

#include "wb.h"
#include "jni/wb_classes.h"

#include <atomic>

/* portbase's activity counters, declared where they are used rather than in a
 * header: three longs whose only consumer is the summary line at the end. */
extern "C" long android_io_assets_opened(void);
extern "C" long android_gl_textures_uploaded(void);
extern "C" long android_gl_draw_calls(void);

/* portbase's viewport scaler (src/symtab_glprobe.cpp); it exports no header.
 * MC3 shipped with this declared and never called - every scale mode was
 * disarmed on non-640x480 panels until a user root-caused it (mc3 issue #2,
 * fix by Codebr0ken). Declared AND called here. */
extern "C" void viewport_scale_init(int phys_w, int phys_h);

/* The library, and where it sits in the player's tree. */
static const char *kNativeLib    = "libUnrealEngine3.so";
static const char *kNativeLibDir = "lib/armeabi-v7a";

/* The R36S panel. The engine takes its resolution from the resize call below
 * and this port never changes it afterwards. The Android wrapper overrides it
 * (<PREFIX>_WIDTH / _HEIGHT) to match the phone's aspect ratio. */
static int kWidth  = 640;
static int kHeight = 480;

/* ---------------------------------------------------------- the module */

static so_module *g_module;

so_module *wb_module(void) { return g_module; }

/*
 * port_guest_module - one of the ten. Deliberately C++ linkage; portbase's
 * pthread thunks resolve the game's thread callbacks through it.
 */
so_module *port_guest_module(void) { return g_module; }

/*
 * Every import nothing answers, named up front.
 *
 * The loader points unresolved jump slots at a stub that aborts on first use,
 * which surfaces one missing symbol per run, from inside a crash, with no
 * stack. Walking the table here lists all of them at once - and it is the fact
 * the harness reads to decide M2.
 *
 * Undefined *weak* symbols are not failures: resolving them to zero is what a
 * real dynamic linker does and the game tests them before use.
 */
static int report_unresolved_symbols(so_module *mod)
{
    int missing = 0;

    for (int i = 0; i < mod->num_dynsym; i++) {
        Elf_Sym *sym = &mod->dynsym[i];
        if (sym->st_shndx != SHN_UNDEF)
            continue;

        const char *name = mod->dynstr + sym->st_name;
        if (!name || !*name)
            continue;

        if (so_resolve_link(mod, name))
            continue;

        if (ELF32_ST_BIND(sym->st_info) == STB_WEAK) {
            trace("weak import left null: %s", name);
            continue;
        }

        fprintf(stderr, "unresolved symbol: %s\n", name);
        missing++;
    }

    fflush(stderr);
    return missing;
}

extern "C" int so_after_relocate(so_module *mod)
{
    g_module = mod;
    trace("module loaded");

    /*
     * Arm the fault handler here rather than after so_load_module() returns.
     * The 740 bytes of INIT_ARRAY this module declares run immediately after
     * this hook; a fault in one of them without a handler installed is
     * reported by qemu as a bare "uncaught target signal 11" with no pc and no
     * stack - the whole first half of the boot as a blind spot, exactly where
     * the engine's own code starts running.
     */
    crash_report_init(mod, kNativeLib);

    /* Before the static initialisers, which may already open files. */
    wb_bridge_private_stdio(mod);

    int missing = report_unresolved_symbols(mod);
    if (missing == 0)
        return 0;

    fatal("%d import(s) of %s have no implementation (listed above).\n"
          "       Running the game now would fault on the first call to any of\n"
          "       them, from inside a static constructor, with nothing but an\n"
          "       address to go on.",
          missing, mod->soname ? mod->soname : "the module");
    return 1;
}

/* ------------------------------------------------------------- the frame */

static SDL_Window *g_window;
static std::atomic<long> g_frames;      /* nativeRender calls completed */
static std::atomic<long> g_presented;   /* frames the engine asked to show */
static std::atomic<bool> g_movie_finished;
static void (*g_video_finished)(JNIEnv *, jclass);

static void present_frame(void)
{
    long n = ++g_presented;

    if (!g_window)
        return;

    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(g_window, &w, &h);
    android_fb_probe(n, w, h);
    android_cursor_draw(w, h);
    emulator_control_after_draw(n, w, h);
    if (gl_bridge_enabled())
        gl_bridge_swap();
    else
        SDL_GL_SwapWindow(g_window);
}

/* Set when the engine presented a frame itself during the current nativeRender. */
static std::atomic<bool> g_swapped_in_frame;

void wb_present(void)
{
    static long calls;
    if (++calls <= 3)
        trace("EglSwapR #%ld on thread %ld%s", calls, (long)syscall(SYS_gettid),
              syscall(SYS_gettid) == getpid() ? " (main)" : "");
    g_swapped_in_frame = true;
    present_frame();
}

void wb_movie_finished(_JNIEnv *env)
{
    (void)env;
    /* Not from inside the callback: the engine is in the middle of starting
     * the movie. The frame loop delivers it before the next frame. */
    g_movie_finished = true;
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    /* The launcher asks `wb --version` to name the build in the log; the
     * field reports on v1.0.0/v1.0.1 all say "vunknown" because this answer
     * was never wired. Before anything else: the loader must not start over
     * a version query. */
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("%s\n", WB_PORT_VERSION);
        return 0;
    }

    /* Unbuffered from the first line: the log is the only diagnostic that
     * leaves the console, and a crash must not take it with it. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    trace("Wild Blood port v%s", WB_PORT_VERSION);

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <wild-blood-directory>\n"
                "\n"
                "The directory is your own copy of the game: lib/armeabi-v7a/ from\n"
                "the APK and the package directory of the unpacked cache\n"
                "(com.gameloft.android.ANMP.GloftWBHM/files). It is never bundled\n"
                "with this port.\n",
                argv[0]);
        return 2;
    }

    const char *game_dir = argv[1];

    /* Before anything of the game's runs: the libc path thunks translate
     * against this, and the engine opens its first file from inside a static
     * initialiser. */
    io_set_game_dir(game_dir);
    wb_prepare_writable_storage();
    {
        long w = port_getenv_long("WIDTH", kWidth), h = port_getenv_long("HEIGHT", kHeight);
        if (w >= 320 && w <= 4096 && h >= 240 && h <= 4096) {
            kWidth = (int)w;
            kHeight = (int)h;
        }
        /* The viewport scaler compares the drawable with this logical size;
         * keeping them equal makes it the identity. */
        char dim[16];
        snprintf(dim, sizeof(dim), "%d", kWidth);
        setenv(PORT_ENV_PREFIX "_SCREEN_W", dim, 0);
        snprintf(dim, sizeof(dim), "%d", kHeight);
        setenv(PORT_ENV_PREFIX "_SCREEN_H", dim, 0);
    }

    char lib_dir[PATH_MAX];
    char lib_path[PATH_MAX];
    snprintf(lib_dir,  sizeof(lib_dir),  "%s/%s", game_dir, kNativeLibDir);
    snprintf(lib_path, sizeof(lib_path), "%s/%s", lib_dir, kNativeLib);

    struct stat st;
    if (stat(lib_path, &st) != 0) {
        fatal("'%s' does not exist.\n"
              "       This does not look like an extracted Wild Blood tree.",
              lib_path);
        return 1;
    }
    trace("native library found: %s (%lld bytes)", lib_path, (long long)st.st_size);
    trace("resource directory: %s", wb_resource_dir());

    /*
     * GL first, before the module is linked.
     *
     * The game's 100 gl* imports are bound by asking the driver for each entry
     * point, and there is no driver to ask until a context is current.
     * Relocating first would bind every one of them to null.
     *
     * A failure here is deliberately not fatal: "does every import resolve?"
     * and "is there a usable GLES context?" are different questions, and
     * aborting on the second makes the first unanswerable from the log.
     */
    SDL_Window   *window = NULL;
    SDL_GLContext gl     = NULL;

    /* Under the Android wrapper the pads are read by the Java side and arrive as commands; SDL's own
     * joystick subsystem would only keep probing for libudev (hundreds of failed opens a second,
     * each a guest syscall out of translated code). */
    const Uint32 sdl_flags = SDL_INIT_VIDEO | (gl_bridge_enabled() ? 0 : SDL_INIT_GAMECONTROLLER);
    if (SDL_Init(sdl_flags) != 0) {
        trace("SDL_Init(0x%x) failed: %s", sdl_flags, SDL_GetError());
    } else {
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

        window = SDL_CreateWindow("Wild Blood",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  kWidth, kHeight,
                                  gl_bridge_enabled() ? 0 : SDL_WINDOW_OPENGL);
        g_window = window;
        if (!window)
            trace("SDL_CreateWindow failed: %s", SDL_GetError());
        else if (gl_bridge_enabled()) {
            /* The GL context lives in the host app; here it is only a token. */
            if (!gl_bridge_init()) {
                fatal("could not connect to the GL host.");
                return 1;
            }
            gl = (SDL_GLContext)(uintptr_t)1;
        }
        else if (!(gl = SDL_GL_CreateContext(window)))
            trace("no GLES 2.0 context: %s", SDL_GetError());
    }

    if (gl) {
        load_gles2_funcs();

        /*
         * The GLES1 table has to be filled too, and not because this game
         * imports GLES1. It imports none - all 100 of its gl* imports are
         * shader-pipeline calls.
         *
         * portbase resolves several of its own diagnostics through
         * find_gles1_function() and nothing else: the framebuffer probe is one
         * of them, and with the table empty it prints
         *
         *     framebuffer probe disabled: GL entry points unresolved
         *
         * and the run reports a black screen while the engine is uploading 755
         * textures and issuing 4819 draws. That is the probe being blind, not
         * the game being black, and the two are indistinguishable from a log
         * that only shows the counters.
         *
         * GL_SINGLE_DISPATCH is what makes filling it safe. It is portbase's
         * own switch, and it forces every GLES1-table name through
         * SDL_GL_GetProcAddress instead of dlsym on the GLESv1_CM provider, so
         * everything lands in the same dispatch as the GLES2 context SDL gave
         * us. Without it the emulator can bind them in desktop GL while SDL
         * uses GLES2, two ABIs that share no state; on the device libmali
         * exports EGL, GLESv1_CM and GLESv2 from one blob and both routes are
         * the same pointer. Set as a default rather than an override, so an
         * explicit WB_GL_SINGLE_DISPATCH still wins for A/B runs.
         */
        setenv("WB_GL_SINGLE_DISPATCH", "1", 0);
        load_gles1_funcs();

        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(window, &w, &h);
        trace("drawable %dx%d", w, h);
        viewport_scale_init(w, h);

        const GLubyte *(*get_string)(GLenum) =
            (const GLubyte *(*)(GLenum))SDL_GL_GetProcAddress("glGetString");
        if (get_string) {
            const char *ver = (const char *)get_string(0x1F02 /* GL_VERSION  */);
            const char *rnd = (const char *)get_string(0x1F01 /* GL_RENDERER */);
            trace("GL_VERSION=%s | GL_RENDERER=%s", ver ? ver : "?", rnd ? rnd : "?");
        }
    }

    /* The fake JavaVM has to exist before the module is linked: the engine's
     * static initialisers run during so_load_module() and reach for it. */
    JavaVM *vm  = NULL;
    JNIEnv *env = NULL;
    if (JNI_CreateJavaVM(&vm, &env, NULL) != JNI_OK || !vm || !env) {
        fatal("could not create the JNI environment.");
        return 1;
    }

    /*
     * Map, relocate and link.
     *
     * The VM argument is NULL on purpose and it is not an oversight:
     * so_load_module() calls JNI_OnLoad itself when a module exports one, and
     * for this game that would run it in the wrong place. The working boot
     * sequence puts JNI_OnLoad first on the game thread, ahead of everything
     * below - passing NULL here leaves that call to us.
     */
    so_set_options(NULL, lib_dir);

    so_module *mod = so_load_module(kNativeLib, NULL, NULL);
    if (!mod) {
        fatal("could not load '%s' from '%s'.", kNativeLib, lib_dir);
        fflush(NULL);
        _exit(1);
    }

    trace("so_load_module returned (text_base=%p size=%zu)",
          (void *)mod->text_base, (size_t)mod->text_size);
    /* Lets a guest-PC profile (QEMU_GUEST_PROF) be mapped back to this executable's symbols. */
    trace("port main() is at %p", (void *)&main);
    trace("libc addrs: memcpy=%p memset=%p strlen=%p sinf=%p mutex_lock=%p",
          (void *)&memcpy, (void *)&memset, (void *)&strlen, (void *)(float (*)(float))&sinf,
          (void *)&pthread_mutex_lock);

    /* ---------------------------------------------------------------- *
     * The boot sequence, in the game's own order.
     * ---------------------------------------------------------------- */

    auto JNI_OnLoad = (int (*)(JavaVM *, void *))so_symbol(mod, "JNI_OnLoad");
    auto Game_nativeInit = (void (*)(JNIEnv *, jclass))
        so_symbol(mod, WB_JNI("Game", "nativeInit"));
    auto Renderer_nativeInit = (void (*)(JNIEnv *, jclass, jint, jint, jint, jstring))
        so_symbol(mod, WB_JNI("GameRenderer", "nativeInit"));
    auto Renderer_nativeResize = (void (*)(JNIEnv *, jclass, jint, jint))
        so_symbol(mod, WB_JNI("GameRenderer", "nativeResize"));
    auto Renderer_nativeRender = (void (*)(JNIEnv *, jclass))
        so_symbol(mod, WB_JNI("GameRenderer", "nativeRender"));
    g_video_finished = (void (*)(JNIEnv *, jclass))
        so_symbol(mod, WB_JNI("MyVideoView", "videoFinished"));

    /* The engine's own "a movie is on screen" flag, for the log. */
    auto is_movie_playing = (unsigned char (*)(void))so_symbol(mod, "nativeIsMoviePlaying");

    if (!JNI_OnLoad || !Game_nativeInit || !Renderer_nativeInit || !Renderer_nativeRender) {
        fatal("%s is missing one of the entry points the port drives:\n"
              "       JNI_OnLoad=%p Game.nativeInit=%p GameRenderer.nativeInit=%p nativeRender=%p",
              kNativeLib, (void *)JNI_OnLoad, (void *)Game_nativeInit,
              (void *)Renderer_nativeInit, (void *)Renderer_nativeRender);
        fflush(NULL);
        _exit(1);
    }

    /* A real class object, not a sentinel: Game.nativeInit() looks its
     * callbacks up on the class it is handed, and GameRenderer.nativeInit()
     * keeps a global reference to its own. */
    jclass game_class = (jclass)&GloftGame::clazz;

    trace("-> JNI_OnLoad at +0x%08lx",
          (unsigned long)((uintptr_t)JNI_OnLoad - mod->text_base));
    JNI_OnLoad(vm, NULL);
    trace("JNI_OnLoad returned");

    /* The licence bookkeeping, before the first thing that writes through it.
     * See game/patch.cpp. */
    wb_unlock_drm(mod);
    wb_tweaks_install(mod);

    android_input_init(mod, env, kWidth, kHeight);

    Game_nativeInit(env, game_class);
    trace("Game.nativeInit returned");

    /* GameRenderer.b(): Game.getManufacture(), the surface, and the version
     * name from the package (Game.aC = getVersionName()). */
    Renderer_nativeInit(env, game_class, 2, kWidth, kHeight, env->NewStringUTF("1.1.4"));
    trace("GameRenderer.nativeInit(%d, %d) returned", kWidth, kHeight);

    if (Renderer_nativeResize) {
        Renderer_nativeResize(env, game_class, kWidth, kHeight);
        trace("GameRenderer.nativeResize(%d, %d) returned", kWidth, kHeight);
    }

    emulator_control_init();

    /*
     * A bounded run. <PREFIX>_FRAME_LIMIT stops the process after that many
     * frames so an automated run terminates on a fact rather than on a
     * stopwatch; unset - the normal case for a player - means run forever.
     */
    const long frame_limit = port_getenv_long("FRAME_LIMIT", 0);

    long frames = 0;
    while (frame_limit == 0 || frames < frame_limit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (!android_input_event(&ev))
                goto done;
        }

        /* The game's own exit request, read next to the event drain rather
         * than trusted to it: a full SDL queue drops the SDL_QUIT that
         * android_app_request_exit() pushes, and a dropped exit is a freeze. */
        if (android_app_exit_requested())
            goto done;

        if (!emulator_control_tick(frames))
            goto done;
        wb_sched_tick(frames);
        android_input_tick();

        if (g_movie_finished.exchange(false) && g_video_finished) {
            g_video_finished(env, game_class);
            trace("MyVideoView.videoFinished returned");
        }

        /* Entering and returning, for the first few. The first call is the
         * engine's whole start-up and takes a long time under emulation. */
        if (frames < 5)
            trace("-> GameRenderer.nativeRender #%ld", frames + 1);

        g_swapped_in_frame = false;
        gl_bridge_profile_step(1);
        Renderer_nativeRender(env, game_class);
        gl_bridge_profile_step(0);
        frames++;
        g_frames = frames;

        /*
         * The swap. The engine's own PlatformSwapBuffers(FES2Viewport*) is a
         * single `bx lr` in this build (+0x014aa05c): on Android the frame is
         * shown by the GLSurfaceView, which swaps after every onDrawFrame, that
         * is after every nativeRender. Game.EglSwapR() is only the extra swap
         * of the loading screens, drawn from inside a long nativeRender; when
         * one of those has just shown the frame, a second swap here would show
         * an undefined back buffer.
         */
        if (!g_swapped_in_frame)
            present_frame();

        if (frames <= 5)
            trace("<- GameRenderer.nativeRender #%ld returned (presented %ld)", frames, g_presented.load());

        if (frames <= 5 || frames % 50 == 0)
            trace("frames=%ld presented=%ld movie=%d", frames, g_presented.load(),
                  is_movie_playing ? (int)is_movie_playing() : -1);
    }

done:
    emulator_control_shutdown(frames);
    trace("frames=%ld presented=%ld", frames, g_presented.load());
    /* The three counters the harness reads to tell "the engine is rendering
     * its own content" apart from "the loader cleared the screen 600 times".
     * A solid-colour clear passes a pixel test on its own; assets opened and
     * textures uploaded are what a game that found its data looks like. */
    trace("summary assets=%ld textures=%ld draws=%ld",
          android_io_assets_opened(), android_gl_textures_uploaded(),
          android_gl_draw_calls());
    trace("run finished: %ld frames (%s)", frames,
          android_app_exit_requested() ? "the game asked to exit"
                                       : "the loader stopped driving it");

    /*
     * The engine started threads of its own and they are still running.
     * Tearing the window and the GL context down from here would pull them out
     * from under those threads and produce a segfault that has nothing to do
     * with why the loader stopped. Leave it to the kernel.
     */
    fflush(NULL);
    _exit(0);
}
