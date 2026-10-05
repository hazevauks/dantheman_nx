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
#define HB "com/halfbrick/mortar/HBSupport"
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

/* NativeGameLib.native_threadEntry(int): how every thread of the engine
 * starts. Its pthread attaches itself to the VM and calls this native method
 * THROUGH Java (CallStaticVoidMethod), so that the thread has a Java frame
 * under it; the native itself is registered from JNI_OnLoad and runs the
 * thread's body, returning when the thread ends. Without this no engine
 * thread does anything. */
H(h_threadEntry) {
  void (*entry)(void *env, void *cls, jint id) =
      (void (*)(void *, void *, jint))jni_native(GL, "native_threadEntry");
  if (!entry) {
    debugPrintf("[java] native_threadEntry(%d): not registered -- the thread does nothing\n",
                (int)a[0].i);
    return jv_none();
  }
  entry(g_jni_env, g_gamelib, a[0].i);
  return jv_none();
}

/* ------------------------------------------------------------- HBSupport */
/* com.halfbrick.mortar.HBSupport: what the phone is. The identifiers are
 * constants: nothing here is sent anywhere, and a save made on one console
 * then reads the same on another. */
#define DEVICE_ID "dantheman-nx-0000000000000000"
H(h_device_id) { return jv_l(jni_str(DEVICE_ID)); }
H(h_uuid) { return jv_l(jni_str("00000000-0000-4000-8000-000000000000")); }
/* Build.VERSION.SDK_INT, as text: Android 6.0 */
H(h_android_version) { return jv_l(jni_str("23")); }
/* PackageInfo.versionCode as text, and versionName: the APK's own */
H(h_package_version) {
  if (dcr_manifest_loaded() && dcr_manifest_version_code() > 0)
    return jv_l(jni_str_fmt("%d", dcr_manifest_version_code()));
  return jv_l(jni_str("1210006"));
}
H(h_package_version_short) {
  const char *v = dcr_manifest_loaded() ? dcr_manifest_version_name() : NULL;
  return jv_l(jni_str(v && v[0] ? v : "1.2.1"));
}
H(h_model) { return jv_l(jni_str("Switch")); }
H(h_manufacturer) { return jv_l(jni_str("Nintendo")); }
/* Locale.getDefault(): the console's, or config.ini's (dtm_locale.c) */
H(h_country) { return jv_l(jni_str(dtm_country())); }
/* language, then "-" country */
H(h_locale) { return jv_l(jni_str(dtm_locale_tag())); }
/* AudioManager: the music stream at full volume (the console's own volume
 * applies after), no headphones reported */
H(h_volume) { return jv_i(15); }
/* /proc/meminfo's MemTotal, in kB: 2 GB */
H(h_total_ram) { return jv_j(2 * 1024 * 1024); }
/* DisplayMetrics.densityDpi: the 6.2" 720p screen */
H(h_density) { return jv_i(240); }
/* 15 no touch screen, 0 one finger, 1 two, 2 distinct, 3 five or more */
H(h_touch_caps) { return jv_i(3); }
/* Configuration.screenLayout: SCREENLAYOUT_SIZE_NORMAL | SCREENLAYOUT_LONG_YES */
H(h_screen_layout) { return jv_i(0x22); }
/* PackageManager.hasSystemFeature: the touch screen's, nothing else */
H(h_has_feature) {
  const char *f = jni_utf(a[0].l);
  const int has = !strncmp(f, "android.hardware.touchscreen", 28);
  debugPrintf("[java] HBSupport.HasSystemFeature(%s) -> %d\n", f, has);
  return jv_z(has);
}

const JMethodDef jni_method_defs[] = {
    {"android/content/Context", "getPackageName", "()" S, h_getPackageName},
    {GA, "GetActivity", "()Landroid/app/Activity;", h_GetActivity},
    /* Android 6's storage permission: granted, and no popup asking for it */
    {GA, "CheckStoragePermission", "()Z", jni_h_true},
    {GA, "CheckShowPopupStoragePermission", "()Z", jni_h_false},
    {GL, "GetSyncObj", "()Ljava/lang/Object;", h_GetSyncObj},
    {GL, "native_threadEntry", "(I)V", h_threadEntry},
    /* the device (InitDeviceProperties, SystemInit) */
    {HB, "GetDeviceID", "()" S, h_device_id},
    {HB, "GetAndroidID", "()" S, h_device_id},
    {HB, "GetUUID", "()" S, h_uuid},
    {HB, "GetAdvertisingId", "()" S, h_uuid},
    {HB, "GetAndroidVersion", "()" S, h_android_version},
    {HB, "GetPackageName", "()" S, h_getPackageName},
    {HB, "GetPackageVersion", "()" S, h_package_version},
    {HB, "GetPackageVersionShort", "()" S, h_package_version_short},
    {HB, "GetModel", "()" S, h_model},
    {HB, "GetManufacturer", "()" S, h_manufacturer},
    {HB, "GetCountry", "()" S, h_country},
    {HB, "GetDeviceLanguage", "()" S, h_locale},
    {HB, "GetDeviceLocale", "()" S, h_locale},
    {HB, "GetWifi", "()I", jni_h_zero},        /* not connected */
    {HB, "IsDeviceTablet", "()I", jni_h_zero}, /* under the Java's diagonal for one */
    {HB, "GetDeviceTotalRAM", "()J", h_total_ram},
    {HB, "GetDensityDPIType", "()I", h_density},
    {HB, "GetTouchscreenCapabilities", "()I", h_touch_caps},
    {HB, "HasSystemFeature", "(" S ")Z", h_has_feature},
    {HB, "GetTVDevice", "()I", jni_h_zero},    /* not a television */
    {HB, "GetPhysicalScreenSizeTypeMask", "()I", h_screen_layout},
    {HB, "GetMusicStreamMaxVolume", "()I", h_volume},
    {HB, "GetMusicStreamVolume", "()I", h_volume},
    {HB, "AreHeadphonesConnected", "()I", jni_h_zero},
    /* the Java answers true except on one maker's phones; false keeps the
     * engine on its own mixer and MortarAudioMixerOut (dtm_audio.c) */
    {GL, "SupportsOpenSL", "()Z", jni_h_false},
    /* small values the engine keeps between launches (dtm_keystore.c) */
    {"com/halfbrick/mortar/KeyStore", "GetValue", "(" S ")" S, dtm_h_keystore_get},
    {"com/halfbrick/mortar/KeyStore", "SetValue", "(" S S ")Z", dtm_h_keystore_set},
    /* the text field's keyboard (dtm_keyboard.c); its other methods (SetText,
     * SetSelectedRegion: the engine telling the Java's editor what it holds)
     * have nothing to do here */
    {"com/halfbrick/mortar/SoftKeyboard", "ShowKeyboard", "(" S "IIII)V", dtm_h_keyboard_show},
    {"com/halfbrick/mortar/SoftKeyboard", "HideKeyboard", "()V", dtm_h_keyboard_hide},
    {"com/halfbrick/mortar/SoftKeyboard", NULL, NULL, jni_h_void},
    {"org/OpenUDID/OpenUDID_manager", "isInitialized", "()Z", jni_h_true},
    {"org/OpenUDID/OpenUDID_manager", "getOpenUDID", "()" S, h_device_id},
    /* the crash reporter: every method does nothing */
    {"com/halfbrick/mortar/MortarCrashlytics", NULL, NULL, jni_h_void},
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
