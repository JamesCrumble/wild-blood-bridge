/*
 * com/gameloft/android/ANMP/GloftWBHM/Game - the Java half of this game - plus
 * the small classes under the same package.
 *
 * The method names are one run of .rodata in libUnrealEngine3.so (getMac ...
 * PassSubtitle); the descriptors are the ones classes.dex declares for them.
 * All of them are static methods of Game: Game.nativeInit() looks every one up
 * on the class it is handed.
 *
 * Almost everything answers with the quietest defensible constant. The ones
 * that do real work:
 *
 *   EglSwapR           the extra swap of the loading screens (wb_present)
 *   getSDFolder        the directory the game data lives in
 *   GetTextureFormat   which cooked directory the engine reads (ETC: the cache
 *                      holds CookedAndroid_ETC only)
 *   getConfig          the quality profile the Java picks per GPU
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"
#include "jni.h"
#include "jni_internals.h"
#include "app_exit.h"
#include "io_paths.h"
#include "port_env.h"
#include "trace.h"

#include "wb.h"
#include "wb_classes.h"

#define WB_PACKAGE "com/gameloft/android/ANMP/GloftWBHM"

static const char *cstr(jstring s)
{
    return s ? ((String *)s)->str : "";
}

static jobject jstr(JNIEnv *env, const char *s)
{
    return (jobject)env->NewStringUTF(s);
}

/* --------------------------------------------------------------- the frame */

static void Game_EglSwapR(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    wb_present();
}

/* ------------------------------------------------------------ the device */

static jobject Game_getSDFolder(JNIEnv *env, jclass c) { (void)c; return jstr(env, wb_resource_dir()); }

/*
 * Game.GetPhoneInfo() picks the first of PVRTC (1), ATC (2), DXT (3), ETC1 (4)
 * the driver advertises, and the engine reads CookedAndroid_<that>. The cache
 * for this build holds CookedAndroid_ETC only, so the answer is 4 whatever the
 * GPU says (an Adreno would otherwise be sent to a directory that is not there).
 */
static jint Game_GetTextureFormat(JNIEnv *e, jclass c) { (void)e; (void)c; return (jint)port_getenv_long("TEXTURE_FORMAT", 4); }

/* The quality profile GameRenderer picks by GPU and screen: 3, 4, 5, 6, 8 or 10
 * in the game's Java, 3 being the field's initial value. */
static jint Game_getConfig(JNIEnv *e, jclass c) { (void)e; (void)c; return (jint)port_getenv_long("CONFIG", 3); }

static jdouble Game_getRatio(JNIEnv *e, jclass c) { (void)e; (void)c; return 1.0; }

/* 0 other, 1 motorola, 2 samsung, 3 htc, 4 sony, 5 sharp. */
static jint Game_getManufacture(JNIEnv *e, jclass c) { (void)e; (void)c; return 2; }

static jobject Game_GetPhoneManufacturer(JNIEnv *env, jclass c) { (void)c; return jstr(env, "samsung"); }
static jobject Game_GetPhoneModel(JNIEnv *env, jclass c)        { (void)c; return jstr(env, "GT-I9300"); }
static jobject Game_GetPhoneCPUName(JNIEnv *env, jclass c)      { (void)c; return jstr(env, "ARMv7 Processor rev 0 (v7l)"); }
static jfloat  Game_GetPhoneCPUFreq(JNIEnv *e, jclass c)        { (void)e; (void)c; return 1400000.0f; }

static jobject Game_GetPhoneGPUName(JNIEnv *env, jclass c)
{
    (void)c;
    /* GL_RENDERER on Android. <PREFIX>_GPU_NAME lets a profile be forced. */
    const char *name = port_getenv("GPU_NAME");
    return jstr(env, name && *name ? name : "Mali-400 MP");
}

static jobject Game_GetDeviceType(JNIEnv *env, jclass c) { (void)c; return jstr(env, "GT-I9300"); }
static jobject Game_GetDeviceFW(JNIEnv *env, jclass c)   { (void)c; return jstr(env, "4.1.2"); }
static jobject Game_GetCountry(JNIEnv *env, jclass c)    { (void)c; return jstr(env, "US"); }
static jobject Game_getMac(JNIEnv *env, jclass c)        { (void)c; return jstr(env, "DE:AD:00:BE:EF:00"); }
static jobject Game_getIdentifier(JNIEnv *env, jclass c) { (void)c; return jstr(env, "wb000000deadbeef"); }
static jobject Game_getUserAgent(JNIEnv *env, jclass c)  { (void)c; return jstr(env, "Mozilla/5.0 (Linux; Android 4.1.2)"); }
static jobject Game_emptyString(JNIEnv *env, jclass c)   { (void)c; return jstr(env, ""); }

/* The index of the phone's language in the game's own table; 0 is English.
 * <PREFIX>_LANG_INDEX overrides. */
static jint Game_GetPhoneLanguage(JNIEnv *e, jclass c) { (void)e; (void)c; return (jint)port_getenv_long("LANG_INDEX", 0); }

static jlong Game_GetCurrentTime(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (jlong)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* No network: the servers are gone. */
static jint Game_zero(JNIEnv *e, jclass c) { (void)e; (void)c; return 0; }

static jfloat Game_GetKindleDeviceVolume(JNIEnv *e, jclass c) { (void)e; (void)c; return 1.0f; }
static void   Game_SetKindleDeviceVolume(JNIEnv *e, jclass c, jfloat v) { (void)e; (void)c; (void)v; }

static jboolean Game_keepScreenOn(JNIEnv *e, jclass c, jboolean on) { (void)e; (void)c; (void)on; return JNI_TRUE; }

/* ------------------------------------------------------------- the movies */

/*
 * On Android this starts a VideoView over the game and the activity reports
 * the end through MyVideoView.videoFinished(). There is no player here: the
 * movie "ends" at once.
 */
static void Game_StopVideo(JNIEnv *env, jclass clazz)
{
    /* Game.StopVideo() closes the player, and the player's own shutdown is
     * what calls videoFinished() - the engine waits for it. */
    trace("JavaCallback_StopVideo - reported as finished");
    wb_movie_finished(env);
    (void)clazz;
}

static void Game_StartVideo(JNIEnv *env, jclass clazz, jstring name)
{
    trace("JavaCallback_StartVideo('%s') - no video player, reported as finished", cstr(name));
    wb_movie_finished(env);
    (void)clazz;
}


/* ---------------------------------------------------------- the edit text */

static JavaObject g_edit_text;

static jobject Game_CreateEditText(JNIEnv *e, jclass c, jint x, jint y, jint w, jint h)
{
    (void)e; (void)c; (void)x; (void)y; (void)w; (void)h;
    return (jobject)&g_edit_text;
}

static void    Game_objVoid(JNIEnv *e, jclass c, jobject o)                  { (void)e; (void)c; (void)o; }
static void    Game_objInt(JNIEnv *e, jclass c, jobject o, jint i)           { (void)e; (void)c; (void)o; (void)i; }
static void    Game_objStr(JNIEnv *e, jclass c, jobject o, jstring s)        { (void)e; (void)c; (void)o; (void)s; }
static jobject Game_GetTextFromEditText(JNIEnv *env, jclass c, jobject o)    { (void)c; (void)o; return jstr(env, ""); }

static jobject Game_GetBitmapFromText(JNIEnv *e, jclass c, jstring text, jstring font, jint w, jint h)
{
    (void)e; (void)c; (void)font;
    static bool announced = false;
    if (!announced) {
        announced = true;
        trace("GetBitmapFromText('%s', %d, %d) - no system text renderer in this port", cstr(text), w, h);
    }
    return NULL;
}

/* ------------------------------------------------------------- the rest */

static void Game_sendAppToBackground(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    /* Recorded and consumed by the frame loop; see portbase/android/app_exit.h. */
    android_app_request_exit("the game called Game.sendAppToBackground()");
}

static void Game_Exit(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    android_app_request_exit("the game called Game.Exit()");
}

static jboolean Game_false(JNIEnv *e, jclass c) { (void)e; (void)c; return JNI_FALSE; }

static void Game_PrintDebug(JNIEnv *e, jclass c, jstring s)   { (void)e; (void)c; trace("game: %s", cstr(s)); }
static void Game_PassSubtitle(JNIEnv *e, jclass c, jstring s) { (void)e; (void)c; (void)s; }

static void Game_openBrowser(JNIEnv *e, jclass c, jstring u, jint i)
{
    (void)e; (void)c; (void)i;
    trace("openBrowser('%s') ignored", cstr(u));
}

static void Game_voidNone(JNIEnv *e, jclass c)                                   { (void)e; (void)c; }
static void Game_voidInt(JNIEnv *e, jclass c, jint i)                            { (void)e; (void)c; (void)i; }
static void Game_voidInt3(JNIEnv *e, jclass c, jint i, jint j, jint k)           { (void)e; (void)c; (void)i; (void)j; (void)k; }
static void Game_voidStrStr(JNIEnv *e, jclass c, jstring s, jstring t)           { (void)e; (void)c; (void)s; (void)t; }
static void Game_voidStrStrInt(JNIEnv *e, jclass c, jstring s, jstring t, jint i) { (void)e; (void)c; (void)s; (void)t; (void)i; }

#define STR "Ljava/lang/String;"
#define OBJ "Ljava/lang/Object;"
#define M(fn, name, sig) ManagedMethod::RegisterStatic<&fn>(CLS::clazz, name, sig)

#define CLS GloftGame
const ManagedMethod gloftGameMethods[] = {
    M(Game_Exit,                  "Exit",                        "()V"),
    M(Game_voidNone,              "Pause",                       "()V"),
    M(Game_false,                 "isHTCDevice",                 "()Z"),
    M(Game_false,                 "isAmazonDevice",              "()Z"),
    M(Game_getMac,                "getMac",                      "()" STR),
    M(Game_getIdentifier,         "getIdentifier",               "()" STR),
    M(Game_GetCountry,            "GetCountry",                  "()" STR),
    M(Game_GetDeviceType,         "GetDeviceType",               "()" STR),
    M(Game_GetDeviceFW,           "GetDeviceFW",                 "()" STR),
    M(Game_sendAppToBackground,   "sendAppToBackground",         "()V"),
    M(Game_openBrowser,           "openBrowser",                 "(" STR "I)V"),
    M(Game_zero,                  "isWifiEnabled",               "()I"),
    M(Game_getConfig,             "getConfig",                   "()I"),
    M(Game_getRatio,              "getRatio",                    "()D"),
    M(Game_GetPhoneLanguage,      "GetPhoneLanguage",            "()I"),
    M(Game_voidNone,              "onLaunchGame",                "()V"),
    M(Game_voidStrStrInt,         "launchGLLive",                "(" STR STR "I)V"),
    M(Game_emptyString,           "getUsername",                 "()" STR),
    M(Game_emptyString,           "getPassword",                 "()" STR),
    M(Game_voidStrStr,            "SaveGliveinfo",               "(" STR STR ")V"),
    M(Game_voidNone,              "popupWelcomeUser",            "()V"),
    M(Game_voidInt,               "popupTrophies",               "(I)V"),
    M(Game_voidNone,              "logoutGLLive",                "()V"),
    M(Game_getManufacture,        "getManufacture",              "()I"),
    M(Game_voidInt,               "launchIGP",                   "(I)V"),
    M(Game_GetCurrentTime,        "GetCurrentTime",              "()J"),
    M(Game_GetTextureFormat,      "GetTextureFormat",            "()I"),
    M(Game_PrintDebug,            "PrintDebug",                  "(" STR ")V"),
    M(Game_GetPhoneManufacturer,  "GetPhoneManufacturer",        "()" STR),
    M(Game_GetPhoneModel,         "GetPhoneModel",               "()" STR),
    M(Game_GetPhoneCPUName,       "GetPhoneCPUName",             "()" STR),
    M(Game_GetPhoneCPUFreq,       "GetPhoneCPUFreq",             "()F"),
    M(Game_GetPhoneGPUName,       "GetPhoneGPUName",             "()" STR),
    M(Game_emptyString,           "getPromoEndTime",             "()" STR),
    M(Game_voidInt,               "SetKeyBar",                   "(I)V"),
    M(Game_GetKindleDeviceVolume, "GetKindleDeviceVolume",       "()F"),
    M(Game_SetKindleDeviceVolume, "SetKindleDeviceVolume",       "(F)V"),
    M(Game_keepScreenOn,          "keepScreenOn",                "(Z)Z"),
    /* EglSwap is an empty method in the game's own Java; EglSwapR swaps at once. */
    M(Game_voidNone,              "EglSwap",                     "()V"),
    M(Game_EglSwapR,              "EglSwapR",                    "()V"),
    M(Game_getSDFolder,           "getSDFolder",                 "()" STR),
    M(Game_getUserAgent,          "getTheUserAgent",             "()" STR),
    M(Game_voidNone,              "GetMemoryState1",             "()V"),
    M(Game_StartVideo,            "JavaCallback_StartVideo",     "(" STR ")V"),
    M(Game_StopVideo,             "JavaCallback_StopVideo",      "()V"),
    M(Game_false,                 "JavaCallback_IsMoviePlaying", "()Z"),
    M(Game_CreateEditText,        "CreateEditText",              "(IIII)" OBJ),
    M(Game_objVoid,               "DestoryEditText",             "(" OBJ ")V"),
    M(Game_objVoid,               "ShowEditText",                "(" OBJ ")V"),
    M(Game_objVoid,               "HideEditText",                "(" OBJ ")V"),
    M(Game_GetTextFromEditText,   "GetTextFromEditText",         "(" OBJ ")" STR),
    M(Game_objStr,                "SetTextToEditText",           "(" OBJ STR ")V"),
    M(Game_objInt,                "ChangePWDInputType",          "(" OBJ "I)V"),
    M(Game_objInt,                "SetTextFilterLen",            "(" OBJ "I)V"),
    M(Game_voidInt3,              "SetProgressBarState",         "(III)V"),
    M(Game_voidInt,               "SetIsAutoLandScape",          "(I)V"),
    M(Game_voidInt,               "SetIsMutipleTouch",           "(I)V"),
    M(Game_GetBitmapFromText,     "GetBitmapFromText",           "(" STR STR "II)" OBJ),
    M(Game_voidNone,              "ShowLoading",                 "()V"),
    M(Game_voidNone,              "HideLoading",                 "()V"),
    M(Game_zero,                  "NetIsEnable",                 "()I"),
    M(Game_voidNone,              "DosetRequestedOrientation",   "()V"),
    M(Game_emptyString,           "displayBriefMemory",          "()" STR),
    M(Game_PassSubtitle,          "PassSubtitle",                "(" STR ")V"),
    {NULL},
};
#undef CLS

Class GloftGame::clazz = {
    .classpath       = WB_PACKAGE "/Game",
    .classname       = "Game",
    .managed_methods = gloftGameMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftGame),
};

/* ------------------------------------------------------------- Device */

#define CLS GloftDevice
const ManagedMethod gloftDeviceMethods[] = {
    M(Game_getUserAgent, "getUserAgent", "()" STR),
    {NULL},
};
#undef CLS

Class GloftDevice::clazz = {
    .classpath       = WB_PACKAGE "/GLUtils/Device",
    .classname       = "Device",
    .managed_methods = gloftDeviceMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftDevice),
};

/* ------------------------------------------------------------- SUtils */

static jobject SUtils_getPreferenceString(JNIEnv *env, jclass clazz, jstring key, jstring prefs)
{
    (void)clazz;
    const char *k = cstr(key);
    trace("SUtils.getPreferenceString('%s', '%s')", k, cstr(prefs));
    /* The installer stores the data directory it unpacked into under this key. */
    if (strcmp(k, "SDFolder") == 0)
        return jstr(env, wb_resource_dir());
    return jstr(env, "");
}

static jobject SUtils_getPackage(JNIEnv *env, jclass c)
{
    (void)c;
    return jstr(env, "com.gameloft.android.ANMP.GloftWBHM");
}

static jobject SUtils_getSaveFolder(JNIEnv *env, jclass c)
{
    (void)c;
    /* Context.getFilesDir() on Android. */
    return jstr(env, io_writable_dir());
}

static jobject SUtils_getContext(JNIEnv *env, jclass c)
{
    (void)env; (void)c;
    static AndroidContext the_context;
    return (jobject)&the_context;
}

#define CLS GloftSUtils
const ManagedMethod gloftSUtilsMethods[] = {
    M(SUtils_getPreferenceString, "getPreferenceString", "(" STR STR ")" STR),
    M(SUtils_getPackage,          "getPackage",          "()" STR),
    M(SUtils_getSaveFolder,       "getSaveFolder",       "()" STR),
    M(Game_getSDFolder,           "getSDFolder",         "()" STR),
    M(SUtils_getContext,          "getContext",          "()Landroid/content/Context;"),
    {NULL},
};
#undef CLS

Class GloftSUtils::clazz = {
    .classpath       = WB_PACKAGE "/GLUtils/SUtils",
    .classname       = "SUtils",
    .managed_methods = gloftSUtilsMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftSUtils),
};

/* ------------------------------------------------------ GameInstaller */

String GloftGameInstaller::mPreferencesName("GloftWBHMPrefs");

const FieldId gloftGameInstallerFields[] = {
    REGISTER_STATIC_FIELD(GloftGameInstaller, mPreferencesName),
    {NULL},
};

#define CLS GloftGameInstaller
const ManagedMethod gloftGameInstallerMethods[] = {
    M(Game_getSDFolder, "getSDFolder", "()" STR),
    M(Game_voidNone,    "startGame",   "()V"),
    {NULL},
};
#undef CLS

Class GloftGameInstaller::clazz = {
    .classpath       = WB_PACKAGE "/installer/GameInstaller",
    .classname       = "GameInstaller",
    .managed_methods = gloftGameInstallerMethods,
    .native_methods  = {NULL},
    .fields          = gloftGameInstallerFields,
    .instance_size   = sizeof(GloftGameInstaller),
};

/* ----------------------------------------- PushNotification/C2DMAndroidUtils */

/* Push notifications: never any, never enabled. appUpdate asks
 * HasPushNotification() every frame. */
static jboolean Push_false(JNIEnv *e, jclass c)                 { (void)e; (void)c; return JNI_FALSE; }
static void     Push_bool(JNIEnv *e, jclass c, jboolean b)      { (void)e; (void)c; (void)b; }
static jint     Push_GetStatus(JNIEnv *e, jclass c, jboolean b) { (void)e; (void)c; (void)b; return 0; }
static jint     Push_strStr(JNIEnv *e, jclass c, jstring a, jstring b) { (void)e; (void)c; (void)a; (void)b; return 0; }
static jint     Push_DeletePush(JNIEnv *e, jclass c, jstring a, jboolean b) { (void)e; (void)c; (void)a; (void)b; return 0; }

static jobject Push_SendPush(JNIEnv *env, jclass c, jobject bundle, jstring a, jstring b, jboolean z)
{
    (void)c; (void)bundle; (void)a; (void)b; (void)z;
    return jstr(env, "");
}

static jobject Push_GetJanusToken(JNIEnv *env, jclass c, jstring a, jstring b, jstring d)
{
    (void)c; (void)a; (void)b; (void)d;
    return jstr(env, "");
}

static jobject Push_GetBundleData(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    static AndroidBundle bundle;
    return (jobject)&bundle;
}

#define CLS GloftPush
const ManagedMethod gloftPushMethods[] = {
    M(Game_zero,          "HasPushNotification",        "()I"),
    M(Game_voidNone,      "ResetNotificationStatus",    "()V"),
    M(Push_bool,          "SetEnable",                  "(Z)V"),
    M(Push_false,         "IsEnable",                   "()Z"),
    M(Push_bool,          "CancelAll",                  "(Z)V"),
    M(Game_emptyString,   "GetTokenID",                 "()" STR),
    M(Game_emptyString,   "GetUserID",                  "()" STR),
    M(Push_GetStatus,     "GetStatus",                  "(Z)I"),
    M(Push_SendPush,      "SendPush",                   "(Landroid/os/Bundle;" STR STR "Z)" STR),
    M(Push_SendPush,      "SendPushToMyself",           "(Landroid/os/Bundle;" STR STR "Z)" STR),
    M(Push_strStr,        "SetOfflineUserCredential",   "(" STR STR ")I"),
    M(Push_strStr,        "SetOnlineUserCredential",    "(" STR STR ")I"),
    M(Game_zero,          "SetOfflineDeviceCredential", "()I"),
    M(Game_zero,          "LogoutUserCredential",       "()I"),
    M(Push_strStr,        "SetUserCredential",          "(" STR STR ")I"),
    M(Push_DeletePush,    "DeletePush",                 "(" STR "Z)I"),
    M(Push_GetBundleData, "GetBundleData",              "()Landroid/os/Bundle;"),
    M(Push_GetJanusToken, "GetJanusToken",              "(" STR STR STR ")" STR),
    M(Push_bool,          "SetDontDisturbEnable",       "(Z)V"),
    M(Push_false,         "IsDontDisturbEnable",        "()Z"),
    M(Game_emptyString,   "GetRegistrationID",          "()" STR),
    {NULL},
};
#undef CLS

Class GloftPush::clazz = {
    .classpath       = WB_PACKAGE "/PushNotification/C2DMAndroidUtils",
    .classname       = "C2DMAndroidUtils",
    .managed_methods = gloftPushMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftPush),
};

/* ---------------------------------------------- com/gameloft/glot/PortingJNI */

/* The tracking library's Java helper: the device's identity, twice. */
#define CLS GlotPortingJNI
const ManagedMethod glotPortingMethods[] = {
    M(Game_getIdentifier, "getIdentifier", "()" STR),
    M(Game_getMac,        "getMac",        "()" STR),
    {NULL},
};
#undef CLS

Class GlotPortingJNI::clazz = {
    .classpath       = "com/gameloft/glot/PortingJNI",
    .classname       = "PortingJNI",
    .managed_methods = glotPortingMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GlotPortingJNI),
};

/* ------------------------------------------------- android/view/KeyEvent */

#define K(name, value) int AndroidKeyEvent::KEYCODE_##name = value;
WB_KEYCODES(K)
#undef K

const FieldId androidKeyEventFields[] = {
#define K(name, value) REGISTER_STATIC_FIELD(AndroidKeyEvent, KEYCODE_##name),
    WB_KEYCODES(K)
#undef K
    {NULL},
};

Class AndroidKeyEvent::clazz = {
    .classpath       = "android/view/KeyEvent",
    .classname       = "KeyEvent",
    .managed_methods = {NULL},
    .native_methods  = {NULL},
    .fields          = androidKeyEventFields,
    .instance_size   = sizeof(AndroidKeyEvent),
};

/* ----------------------------------------------------------- registry */

static const int registered[] = {
    ClassRegistry::register_class(GloftGame::clazz),
    ClassRegistry::register_class(GloftDevice::clazz),
    ClassRegistry::register_class(GloftSUtils::clazz),
    ClassRegistry::register_class(GloftGameInstaller::clazz),
    ClassRegistry::register_class(GloftPush::clazz),
    ClassRegistry::register_class(GlotPortingJNI::clazz),
    ClassRegistry::register_class(AndroidKeyEvent::clazz),
};
