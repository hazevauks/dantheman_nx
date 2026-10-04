/* dtm_java.c -- the Java side of Dan the Man, as the engine sees it.
 *
 * The game's Java (com.halfbrick.mortar: MortarGameActivity, its
 * GLSurfaceView MortarGameView and renderer, GameManager, the input
 * handlers) does not run here: dtm_game.c, dtm_input.c and dtm_audio.c do
 * what it did. What the ENGINE calls back through JNI is answered from the
 * tables below.
 *
 * The engine looks up far more Java than a Switch has a use for: Halfbrick's
 * "bricknet" services (Facebook, Google Play Games, billing, sharing, web
 * views), the ad providers (AdMob, AdColony), analytics (AppsFlyer, Firebase,
 * Crashlytics), Helpshift, push notifications. None has a handler: the JNI
 * core answers an unhandled method with its type's zero (false, 0, null) and
 * logs it once, which reads to the engine as "not available". That log is
 * the to-do list: what the game turns out to need is added here. MIT.
 */
#include <string.h>

#include "config.h"
#include "dcr_manifest.h"
#include "dtm.h"
#include "jni.h"
#include "util.h"

#define GA "com/halfbrick/mortar/MortarGameActivity"
#define GL "com/halfbrick/mortar/NativeGameLib"
#define MX "com/halfbrick/mortar/MortarAudioMixerOut"
#define S "Ljava/lang/String;"

#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

JObj *g_activity;
void *g_gamelib;

H(h_getPackageName) {
  const char *pkg = dcr_manifest_loaded() && dcr_manifest_package()[0] ? dcr_manifest_package()
                                                                        : DTM_PACKAGE;
  return jv_l(jni_str(pkg));
}

/* MortarGameActivity.GetActivity(): sActivity. */
H(h_GetActivity) { return jv_l(jni_retain(g_activity)); }

/* NativeGameLib.GetSyncObj(): the object the Java synchronizes the natives
 * on. One thread calls them here; the engine only needs it to exist. */
H(h_GetSyncObj) { return jv_l(jni_retain(jni_singleton("java/lang/Object"))); }

const JMethodDef jni_method_defs[] = {
    {"android/content/Context", "getPackageName", "()" S, h_getPackageName},
    {GA, "GetActivity", "()Landroid/app/Activity;", h_GetActivity},
    {GL, "GetSyncObj", "()Ljava/lang/Object;", h_GetSyncObj},
    /* the engine's sound output (dtm_audio.c) */
    {MX, "Create", "()L" MX ";", dtm_h_mixer_create},
    {MX, "GetNativeSampleRate", "()I", dtm_h_mixer_rate},
    {MX, "Init", "(I)I", dtm_h_mixer_init},
    {MX, "WriteData", "([S)V", dtm_h_mixer_write},
    {MX, "WriteData", "([B)V", dtm_h_mixer_write},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {GA, "android/support/v4/app/FragmentActivity"},
    {"android/support/v4/app/FragmentActivity", "android/app/Activity"},
    {"android/app/Activity", "android/view/ContextThemeWrapper"},
    {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {NULL, NULL},
};

/* Classes the engine probes for and must not find (when classes.txt is
 * absent): none known yet. */
const char *const jni_missing_classes[] = {
    NULL,
};

void dtm_java_init(void) {
  jni_init();
  g_activity = jni_singleton(GA);
  g_gamelib = jni_class(GL)->obj;
  debugPrintf("[java] MortarGameActivity %p, NativeGameLib %p; package %s\n", (void *)g_activity,
              g_gamelib, dcr_manifest_loaded() ? dcr_manifest_package() : DTM_PACKAGE " (default)");
}
