/* dtm.h -- what Dan the Man's port files share: the engine's module, its
 * natives (com.halfbrick.mortar.NativeGameLib's), and each file's entry
 * points. MIT.
 */
#ifndef DTM_H
#define DTM_H

#include <stdint.h>

#include "jni.h"
#include "so_util.h"

/* ------------------------------------------------------------- the engine */
extern so_module g_mod_game;

/* NativeGameLib's natives, as the Java declares them (classes2.dex): every
 * one is static, so the second argument is the class object. Floats and
 * doubles travel in core registers (softfp), as the engine was built. */
typedef struct {
  void (*InitDeviceProperties)(void *env, void *cls);
  void (*InitFileManager)(void *env, void *cls, void *apk, void *files, void *cache, void *external,
                          jboolean flag);
  jboolean (*InitOpenSLSoundManager)(void *env, void *cls, void *asset_manager);
  void (*InitJavaSoundManager)(void *env, void *cls);
  jint (*GLESVersion)(void *env, void *cls);
  void (*SystemInit)(void *env, void *cls, jint width, jint height, void *language);
  void (*GameInit)(void *env, void *cls);
  jboolean (*step)(void *env, void *cls);
  jboolean (*gameRequestedQuit)(void *env, void *cls);
  jboolean (*gameRequestedRestart)(void *env, void *cls);
  void (*confirmQuitRequest)(void *env, void *cls, jboolean quit);
  void (*saveOnExit)(void *env, void *cls);
  void (*onPause)(void *env, void *cls);
  void (*onResume)(void *env, void *cls, void *pixels, jint width, jint height, jboolean flag);
  jboolean (*onResumeStep)(void *env, void *cls);
  void (*onFocusLost)(void *env, void *cls);
  void (*onFocusRetrieved)(void *env, void *cls);
  void (*SetAppLicensed)(void *env, void *cls, jboolean licensed);
  void (*StoragePermissionResult)(void *env, void *cls, jboolean granted);
  void (*keyEvent)(void *env, void *cls, jint code, jboolean down, jboolean flag, jint device);
  void (*motionEvent)(void *env, void *cls, jint device, jint axis, jfloat x, jfloat y);
  void (*touchEvent)(void *env, void *cls, jint action, jlong time, jint pointer, jfloat x, jfloat y,
                     jfloat pressure, jfloat size);
  void (*onGameControllerAttach)(void *env, void *cls, jint device, void *name);
  void (*onGameControllerDetach)(void *env, void *cls, jint device);
} DtmNatives;
extern DtmNatives g_n;

/* dtm_loader.c */
int dtm_load_engine(void);       /* 0, or negative (logged) */
void dtm_run_constructors(void); /* System.loadLibrary: the init array, JNI_OnLoad */

/* dtm_firebase.c: FirebaseNS replaced, once the module is sealed as code */
void dtm_firebase_patch(void);
/* An engine function (Thumb or ARM, by its address's low bit) replaced at
 * its first instruction by a jump to dst: 0 when patched. */
int dtm_hook(uintptr_t fn, void *dst);

/* dtm_time.c: the game's server time replaced by the console's clock, for
 * the weekly events, once the module is sealed as code */
void dtm_time_patch(void);

/* dtm_locale.c: the console's language, or config.ini's ("pt", "BR", "pt-BR") */
const char *dtm_language(void);
const char *dtm_country(void);
const char *dtm_locale_tag(void);

/* dtm_java.c */
extern JObj *g_activity; /* MortarGameActivity */
extern void *g_gamelib;  /* NativeGameLib's class object: the natives' second argument */
void dtm_java_init(void);

/* dtm_game.c */
int dtm_game_run(void);

/* dtm_audio.c: MortarAudioMixerOut, the engine's PCM through audout */
int dtm_audio_init(void);
void dtm_audio_pause(int paused);
void dtm_audio_shutdown(void);
uint32_t dtm_audio_blocks(void);
/* the Java class's methods, for the handler tables (dtm_java.c) */
JNI_H_DECL(dtm_h_mixer_create);
JNI_H_DECL(dtm_h_mixer_rate);
JNI_H_DECL(dtm_h_mixer_init);
JNI_H_DECL(dtm_h_mixer_write);

/* dtm_keystore.c: com.halfbrick.mortar.KeyStore, in <game folder>/data/keystore.txt */
JNI_H_DECL(dtm_h_keystore_get);
JNI_H_DECL(dtm_h_keystore_set);

/* dtm_input.c */
void dtm_input_init(void);
void dtm_input_poll(int width, int height);
void dtm_input_reset(void); /* focus lost: held keys and touches let go */
/* The controller a player holds now: its style (0: none) and its hid id. */
u64 dtm_input_controller(int player, HidNpadIdType *id);

/* dtm_saves.c: the engine's saves written to the card by a thread of the
 * port's (init before the engine runs; flush when the game leaves the screen
 * or closes) */
void dtm_saves_init(void);
void dtm_saves_flush(void);
void dtm_saves_report(void);

/* dtm_rumble.c: the controller rumbles with the game's camera shakes (the
 * patch once the module is sealed as code; frame from the frame loop) */
void dtm_rumble_patch(void);
void dtm_rumble_frame(void);
void dtm_rumble_stop(void);

/* dtm_keyboard.c: com.halfbrick.mortar.SoftKeyboard as the system keyboard
 * (frame from the frame loop, after the engine's step) */
void dtm_keyboard_frame(void);
JNI_H_DECL(dtm_h_keyboard_show);
JNI_H_DECL(dtm_h_keyboard_hide);

#endif /* DTM_H */
