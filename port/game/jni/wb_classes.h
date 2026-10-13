#pragma once

#include "jni.h"
#include "jni_internals.h"

/*
 * The Java classes this game asks for.
 *
 * Classpath-shaped strings in the library (strings -a libUnrealEngine3.so):
 *
 *   android/os/Bundle, android/view/KeyEvent, android/view/ViewRoot,
 *   android/telephony/TelephonyManager, android/os/Build, android/os/Build$VERSION,
 *   android/os/SystemProperties, android/provider/Settings$Secure,
 *   android/content/Context, java/util/UUID, com/gameloft/glot/PortingJNI,
 *   android/media/AudioTrack
 *
 * plus the ones assembled at runtime from the package prefix
 * "com/gameloft/android/ANMP/GloftWBHM" and a tail: /Game, /GLUtils/SUtils,
 * /GLUtils/Device, /installer/GameInstaller, /PushNotification/C2DMAndroidUtils.
 *
 * Grouped by the file that implements them:
 *
 *   game_classes.cpp     Game (every callback of the engine), SUtils, Device,
 *                        GameInstaller, C2DMAndroidUtils, PortingJNI
 *   audio_track.cpp      android/media/AudioTrack (the game's only audio output)
 *   android_app.cpp      Context, Bundle, Process and the device-id helpers
 *
 * Two rules run through all of them:
 *
 *   - Never return NULL to the engine. It stores what it gets and dereferences
 *     it later.
 *   - Method lookup is an exact strcmp on name AND descriptor. The descriptors
 *     here are the ones the game's own classes.dex declares.
 */

#define WB_CLASS(name)                       \
    class name : public Object {                \
    public:                                     \
        static Class clazz;                     \
        Class *_getClass() { return &clazz; }   \
    }

/* <package>/Game - the activity. Every callback the engine makes into the
 * game's own Java is a static method of this class. */
WB_CLASS(GloftGame);

/* <package>/GLUtils/Device, /GLUtils/SUtils, /PushNotification/C2DMAndroidUtils,
 * com/gameloft/glot/PortingJNI (the tracking library's helper) */
WB_CLASS(GloftDevice);
WB_CLASS(GloftSUtils);
WB_CLASS(GloftPush);
WB_CLASS(GlotPortingJNI);

WB_CLASS(AndroidBundle);
WB_CLASS(AndroidProcess);
WB_CLASS(AndroidSystemProperties);
WB_CLASS(AndroidSettingsSecure);
WB_CLASS(AndroidTelephonyManager);
WB_CLASS(AndroidContentResolver);
WB_CLASS(AndroidViewRoot);
WB_CLASS(JavaUuid);
WB_CLASS(JavaObject);

#undef WB_CLASS

/*
 * The classes the game reads a *field* off, rather than calling.
 *
 * These cannot use the macro above because a field has to exist as a real
 * member to have an address: REGISTER_STATIC_FIELD records &Class::member, and
 * the fake JVM hands the engine that address directly.
 */
class AndroidContext : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }
};

class AndroidBuild : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    /* Read by the device-id fallback chain (be9ca0/be9cb4). */
    static String SERIAL;
};

class AndroidBuildVersion : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    /* Named at be9c30/be9c4c. Declared, deliberately not registered - see the
     * comment on its Class in android_app.cpp. */
    static int SDK_INT;
};

/*
 * android/view/KeyEvent. The engine's initMap() reads these 72 constants by
 * name (GetStaticFieldID + GetStaticIntField) to build its table from Android
 * key codes to its own key names; the values are Android's.
 */
#define WB_KEYCODES(K) \
    K(TAB, 61) K(ENTER, 66) K(SPACE, 62) K(BACK, 4) K(MENU, 82) K(HOME, 3) K(DEL, 67) K(0, 7) \
    K(1, 8) K(2, 9) K(3, 10) K(4, 11) K(5, 12) K(6, 13) K(7, 14) K(8, 15) K(9, 16) K(A, 29) \
    K(B, 30) K(C, 31) K(D, 32) K(E, 33) K(F, 34) K(G, 35) K(H, 36) K(I, 37) K(J, 38) K(K, 39) \
    K(L, 40) K(M, 41) K(N, 42) K(O, 43) K(P, 44) K(Q, 45) K(R, 46) K(S, 47) K(T, 48) K(U, 49) \
    K(V, 50) K(W, 51) K(X, 52) K(Y, 53) K(Z, 54) K(STAR, 17) K(PLUS, 81) K(MINUS, 69) K(NUM, 78) \
    K(ALT_LEFT, 57) K(ALT_RIGHT, 58) K(SHIFT_LEFT, 59) K(SHIFT_RIGHT, 60) K(APOSTROPHE, 75) \
    K(SEMICOLON, 74) K(EQUALS, 70) K(COMMA, 55) K(PERIOD, 56) K(SLASH, 76) K(GRAVE, 68) \
    K(LEFT_BRACKET, 71) K(BACKSLASH, 73) K(RIGHT_BRACKET, 72) K(DPAD_UP, 19) K(DPAD_DOWN, 20) \
    K(DPAD_LEFT, 21) K(DPAD_RIGHT, 22) K(DPAD_CENTER, 23) K(BUTTON_Y, 100) K(BUTTON_R1, 103) \
    K(BUTTON_L1, 102) K(BUTTON_X, 99) K(BUTTON_START, 108) K(BUTTON_SELECT, 109)

class AndroidKeyEvent : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

#define K(name, value) static int KEYCODE_##name;
    WB_KEYCODES(K)
#undef K
};

/*
 * <package>/installer/GameInstaller. The device-id chain reads m_sInstance off
 * it (the installer activity, used as a Context) and mDeviceInfo (a
 * TelephonyManager); mPreferencesName is the preferences file the SD folder is
 * stored in.
 */
class GloftGameInstaller : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    static String mPreferencesName;
};

/*
 * The singletons the engine is handed.
 *
 * On Android these are distinct objects with state; here they exist so that a
 * method looked up on what getContentResolver() returned resolves to
 * something. They are deliberately global rather than allocated per call: the
 * engine keeps the pointers it is given for the life of the process and
 * compares some of them, and a fresh object per call would break that
 * silently.
 */
extern AndroidTelephonyManager g_telephony_manager;
extern AndroidContentResolver  g_content_resolver;
extern AndroidViewRoot         g_view_root;
