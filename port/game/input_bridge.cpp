/*
 * Input for Wild Blood.
 *
 * The game is touch-first. Its Java (GameGLSurfaceView.onTouchEvent) passes
 * every finger to
 *
 *     GameGLSurfaceView.nativeOnTouch(action, x, y, pointerId, eventTimeMs)
 *
 * which is appOnTouch() in the library: action 1 is a finger going down, 2 a
 * move, 0 a finger lifted (the function maps them to the engine's own
 * Touch_Began / Touch_Moved / Touch_Ended through a three-entry table), the
 * coordinates are view pixels and the time ends up in seconds. The events are
 * queued under a mutex and consumed by the next FEngineLoop::Tick, so the call
 * is safe from the frame loop at any point.
 *
 * Keys go to Game.nativeOnKeyDown/Up(keycode) with Android key codes. The
 * gamepad table below is the plain Android one; which codes the engine acts on
 * has not been traced yet.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "platform.h"
#include "so_util.h"
#include "jni.h"

#include "input_bridge.h"
#include <stdlib.h>

#include "port_env.h"
#include "gl_bridge.h"
#include "trace.h"

#include "wb.h"

typedef void (*fn_key_t)(JNIEnv *, jclass, jint);
typedef void (*fn_touch_t)(JNIEnv *, jclass, jint, jint, jint, jint, jlong);

static fn_key_t   g_on_key_down;
static fn_key_t   g_on_key_up;
static fn_touch_t g_on_touch;

static JNIEnv *g_env;
static int     g_width  = 640;
static int     g_height = 480;
static long    g_input_ticks;

/* The engine builds its input manager during appInit(), which is the first
 * nativeRender; nothing is delivered before a few frames have been drawn. */
static const long kInputReady = 3;

enum { TOUCH_UP = 0, TOUCH_DOWN = 1, TOUCH_MOVE = 2 };

static jlong now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (jlong)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool send_touch(int action, int x, int y, int id)
{
    if (g_input_ticks < kInputReady || !g_on_touch)
        return false;
    static int logged;
    if (action != TOUCH_MOVE && logged < 200) {
        logged++;
        trace("input: touch %d at %d,%d id %d", action, x, y, id);
    }
    g_on_touch(g_env, NULL, action, x, y, id, now_ms());
    return true;
}

static void send_key(int code, bool down)
{
    static int logged;
    if (logged < 200) {
        logged++;
        trace("input: key %d %s", code, down ? "down" : "up");
    }
    if (g_input_ticks < kInputReady)
        return;
    fn_key_t fn = down ? g_on_key_down : g_on_key_up;
    if (fn)
        fn(g_env, NULL, code);
}

/* ---------------------------------------------------------------- the cursor */

/* A pointer for hosts with a mouse or a pad: its presses are finger 0. */
static float g_cursor_x = 320.0f;
static float g_cursor_y = 240.0f;
static bool  g_cursor_down;

extern "C" void android_input_cursor_position(float *x, float *y, int *visible)
{
    if (x) *x = g_cursor_x;
    if (y) *y = g_cursor_y;
    /* <PREFIX>_CURSOR=0 hides it for hosts that drive the pointer by touch. */
    static int allowed = -1;
    if (allowed < 0)
        allowed = port_getenv_long("CURSOR", 1) != 0;
    if (visible) *visible = allowed;
}

void android_input_cursor_set(float x, float y)
{
    g_cursor_x = x < 0 ? 0 : (x > g_width  - 1 ? g_width  - 1 : x);
    g_cursor_y = y < 0 ? 0 : (y > g_height - 1 ? g_height - 1 : y);
    if (g_cursor_down)
        send_touch(TOUCH_MOVE, (int)g_cursor_x, (int)g_cursor_y, 0);
}

void android_input_cursor_press(bool down)
{
    if (down == g_cursor_down)
        return;
    g_cursor_down = down;
    send_touch(down ? TOUCH_DOWN : TOUCH_UP, (int)g_cursor_x, (int)g_cursor_y, 0);
}

/* ------------------------------------------------------------------- SDL in */

/* Android KeyEvent codes. */
enum {
    KEY_BACK = 4, KEY_DPAD_UP = 19, KEY_DPAD_DOWN = 20, KEY_DPAD_LEFT = 21, KEY_DPAD_RIGHT = 22,
    KEY_DPAD_CENTER = 23, KEY_BUTTON_A = 96, KEY_BUTTON_B = 97, KEY_BUTTON_X = 99, KEY_BUTTON_Y = 100,
    KEY_BUTTON_L1 = 102, KEY_BUTTON_R1 = 103, KEY_BUTTON_START = 108, KEY_BUTTON_SELECT = 109,
};

static const struct { const char *name; SDL_GameControllerButton button; int code; } kControls[] = {
    { "a",      SDL_CONTROLLER_BUTTON_A,             KEY_BUTTON_A      },
    { "b",      SDL_CONTROLLER_BUTTON_B,             KEY_BUTTON_B      },
    { "x",      SDL_CONTROLLER_BUTTON_X,             KEY_BUTTON_X      },
    { "y",      SDL_CONTROLLER_BUTTON_Y,             KEY_BUTTON_Y      },
    { "l1",     SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  KEY_BUTTON_L1     },
    { "r1",     SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, KEY_BUTTON_R1     },
    { "start",  SDL_CONTROLLER_BUTTON_START,         KEY_BUTTON_START  },
    { "select", SDL_CONTROLLER_BUTTON_BACK,          KEY_BUTTON_SELECT },
    { "up",     SDL_CONTROLLER_BUTTON_DPAD_UP,       KEY_DPAD_UP       },
    { "down",   SDL_CONTROLLER_BUTTON_DPAD_DOWN,     KEY_DPAD_DOWN     },
    { "left",   SDL_CONTROLLER_BUTTON_DPAD_LEFT,     KEY_DPAD_LEFT     },
    { "right",  SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    KEY_DPAD_RIGHT    },
    { "back",   SDL_CONTROLLER_BUTTON_INVALID,       KEY_BACK          },
};

bool android_input_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_QUIT:
        return false;

    case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(event->cdevice.which);
        return true;

    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        for (unsigned i = 0; i < sizeof(kControls) / sizeof(kControls[0]); i++) {
            if (kControls[i].button == event->cbutton.button) {
                send_key(kControls[i].code, event->type == SDL_CONTROLLERBUTTONDOWN);
                break;
            }
        }
        return true;

    case SDL_MOUSEMOTION:
        android_input_cursor_set((float)event->motion.x, (float)event->motion.y);
        return true;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        android_input_cursor_set((float)event->button.x, (float)event->button.y);
        android_input_cursor_press(event->type == SDL_MOUSEBUTTONDOWN);
        return true;

    default:
        return true;
    }
}

void android_input_tick(void)
{
    g_input_ticks++;
}

/* ------------------------------------------------------- the host's channel */

bool android_input_inject_control(const char *name, bool down)
{
    if (!name)
        return false;
    for (unsigned i = 0; i < sizeof(kControls) / sizeof(kControls[0]); i++) {
        if (strcmp(kControls[i].name, name) == 0) {
            send_key(kControls[i].code, down);
            return true;
        }
    }
    return false;
}

/* Raw touch from the host: action 1 down, 2 move, 0 up; view pixels; pointer id. */
bool android_input_inject_touch(int action, int x, int y, int id)
{
    return send_touch(action, x, y, id);
}

bool android_input_inject_key(int code, bool down)
{
    if (g_input_ticks < kInputReady)
        return false;
    send_key(code, down);
    return true;
}

/* The engine has an analogue path (Game.nativeOnMotionEventPowerA); it is not wired yet. */
bool android_input_inject_stick(const char *name, float x, float y)
{
    (void)name; (void)x; (void)y;
    return false;
}

void android_input_set_pad(bool connected)
{
    trace("input: pad %s (the gamepad path is not wired yet)", connected ? "connected" : "disconnected");
}

bool android_input_in_menu(void)
{
    return false;
}

/* The autopilot of the handheld ports is not used here. */
void android_input_autopilot_tick(long frame)   { (void)frame; }
void android_input_autopilot_sample(long frame) { (void)frame; }
long android_input_autopilot_keys(void)         { return 0; }
long android_input_autopilot_scenes(void)       { return 0; }

/* ------------------------------------------------------------------- wiring */

void android_input_init(so_module *mod, _JNIEnv *env, int width, int height)
{
    g_env    = (JNIEnv *)env;
    g_width  = width;
    g_height = height;
    g_cursor_x = width  / 2.0f;
    g_cursor_y = height / 2.0f;

    g_on_key_down = (fn_key_t)  so_symbol(mod, WB_JNI("Game", "nativeOnKeyDown"));
    g_on_key_up   = (fn_key_t)  so_symbol(mod, WB_JNI("Game", "nativeOnKeyUp"));
    g_on_touch    = (fn_touch_t)so_symbol(mod, WB_JNI("GameGLSurfaceView", "nativeOnTouch"));

    trace("input: keys=%p/%p touch=%p", (void *)g_on_key_down, (void *)g_on_key_up, (void *)g_on_touch);

    if (!gl_bridge_enabled())
        SDL_GameControllerOpen(0);
}
