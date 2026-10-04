/* dtm_game.c -- plays the part of the game's Java: MortarGameActivity, its
 * GLSurfaceView (MortarGameView), the renderer and GameManager.
 *
 * What the Java does, and what this file does for it:
 *
 *   GLSurfaceView            an EGL context: OpenGL ES 2, the window surface
 *                            (here: libnx's default window, through the
 *                            shared EGL layer, gl_mesa.c)
 *   GameManager.SystemInit   on the GL thread, over the first frames:
 *                              NativeGameLib.InitDeviceProperties()  (an
 *                                AsyncTask there; in line here)
 *                              InitFileManager(apk, files/, cache/,
 *                                external/, false): the engine opens the APK
 *                                itself and reads assets/ from it
 *                              InitOpenSLSoundManager(assets), and when that
 *                                fails InitJavaSoundManager()
 *                              SystemInit(width, height, language)
 *   GameManager.GameInit     NativeGameLib.GameInit(), once
 *   GameManager.Render       each frame: the queued keys (keyEvent), then
 *                            step(); false closes the game. Then
 *                            gameRequestedQuit() -> the "quit?" dialog and
 *                            confirmQuitRequest(); gameRequestedRestart()
 *   Activity.onPause         NativeGameLib.onPause(), saveOnExit()
 *   Activity.onResume        the view resumes; the renderer then calls
 *                            NativeGameLib.onResume(...) / onResumeStep()
 *
 * The paths are Android's own: the runtime turns them into the game folder's
 * on the SD card (dcr_path.c), and the APK's into the player's own APK.
 *
 * On Android the UI thread (input) and the GL thread (frames) run side by
 * side; here one thread does both, in the order a frame sees them: input,
 * step, present. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "dtm.h"
#include "error.h"
#include "gl_layer.h"
#include "jni.h"
#include "rt_applet.h"
#include "rt_window.h"
#include "util.h"
#include "watchdog.h"

#define ENV g_jni_env
#define CLS g_gamelib

/* The shared EGL layer (gl_mesa.c / gl_null.c), as plain C: both define
 * these with the same register-level signatures. */
typedef int32_t fEGLint;
void *b_eglGetDisplay(void *native);
unsigned b_eglInitialize(void *d, fEGLint *maj, fEGLint *min);
unsigned b_eglChooseConfig(void *d, const fEGLint *attrs, void **cfgs, fEGLint cap, fEGLint *num);
void *b_eglCreateWindowSurface(void *d, void *cfg, void *win, const fEGLint *attrs);
void *b_eglCreateContext(void *d, void *cfg, void *share, const fEGLint *attrs);
unsigned b_eglMakeCurrent(void *d, void *draw, void *read, void *ctx);
unsigned b_eglSwapInterval(void *d, fEGLint interval);
unsigned b_eglSwapBuffers(void *d, void *s);
unsigned b_eglDestroySurface(void *d, void *s);
unsigned b_eglDestroyContext(void *d, void *c);
unsigned b_eglTerminate(void *d);
fEGLint b_eglGetError(void);

#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0

static volatile int g_exit;
static int g_engine_up; /* GameInit done: the natives may be called */
static int g_w, g_h;
static void *g_dpy, *g_surf, *g_ctx;

/* For the watchdog: frames presented. */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

/* --------------------------------------------------------- lifecycle */
/* The runtime's applet lifecycle (rt_applet.c) calls these from the frame
 * loop's rt_applet_poll(): what MortarGameActivity's onPause / onResume did.
 * Focus lost: held keys and touches let go, the engine paused and its save
 * written (as the Java does at every onPause), the sound held. */
void port_focus_lost(void) {
  if (!g_engine_up)
    return;
  dtm_input_reset();
  if (g_n.onFocusLost)
    g_n.onFocusLost(ENV, CLS);
  g_n.onPause(ENV, CLS);
  if (g_n.saveOnExit)
    g_n.saveOnExit(ENV, CLS);
  dtm_audio_pause(1);
}

/* The GL context was never lost here, so the engine has nothing to reload:
 * no startup texture (NULL, 0x0). It may still take some steps to resume
 * (onResumeStep), which the frame loop runs. */
static int g_resuming;
void port_focus_gained(void) {
  if (!g_engine_up)
    return;
  dtm_audio_pause(0);
  g_n.onResume(ENV, CLS, NULL, 0, 0, 0);
  if (g_n.onFocusRetrieved)
    g_n.onFocusRetrieved(ENV, CLS);
  g_resuming = 1;
}

/* HOME and sleep freeze the whole process; the runtime's clocks find each
 * freeze. What Android does around it: onPause, then onResume. */
void port_process_frozen(unsigned count) {
  debugPrintf("[game] the process was held (HOME menu or sleep; freeze %u)\n", count);
  port_focus_lost();
  port_focus_gained();
}

/* ---------------------------------------------------------------- EGL */
static void egl_up(void) {
  dcr_window_size(&g_w, &g_h);
  g_dpy = b_eglGetDisplay(NULL);
  fEGLint maj = 0, min = 0, n = 0;
  if (!g_dpy || !b_eglInitialize(g_dpy, &maj, &min))
    fatal_error("The graphics driver did not start (eglInitialize 0x%x).", (unsigned)b_eglGetError());
  unsigned (*bind_api)(unsigned) = (unsigned (*)(unsigned))dcr_gl_lookup("eglBindAPI");
  if (bind_api)
    bind_api(EGL_OPENGL_ES_API);
  /* MortarGameView's chooser: ES 2, RGB 8, depth and stencil (the engine
   * enables GL_DEPTH_TEST in every frame's set-up) */
  static const fEGLint cfg_attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE,
                                      EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                      EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                                      EGL_NONE};
  void *cfg = NULL;
  if (!b_eglChooseConfig(g_dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
    fatal_error("No OpenGL ES 2 window configuration (0x%x).", (unsigned)b_eglGetError());
  g_surf = b_eglCreateWindowSurface(g_dpy, cfg, nwindowGetDefault(), NULL);
  static const fEGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  g_ctx = b_eglCreateContext(g_dpy, cfg, NULL, ctx_attrs);
  if (!g_surf || !g_ctx || !b_eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx))
    fatal_error("Could not create the OpenGL ES 2 context (surface %p, context %p, 0x%x).", g_surf,
                g_ctx, (unsigned)b_eglGetError());
  b_eglSwapInterval(g_dpy, 1);
  debugPrintf("[game] EGL %d.%d: OpenGL ES 2 on the window, %dx%d\n", (int)maj, (int)min, g_w, g_h);
}

static void egl_down(void) {
  if (!g_dpy)
    return;
  b_eglMakeCurrent(g_dpy, NULL, NULL, NULL);
  if (g_ctx)
    b_eglDestroyContext(g_dpy, g_ctx);
  if (g_surf)
    b_eglDestroySurface(g_dpy, g_surf);
  b_eglTerminate(g_dpy);
  g_dpy = g_surf = g_ctx = NULL;
}

/* ---------------------------------------------------------------- report */
static void report(void) {
  static u64 last_tick;
  static unsigned long last_frames;
  const u64 tick = armGetSystemTick();
  const unsigned long frames = (unsigned long)dcr_gl_frames();
  const double fps =
      last_tick ? (double)(frames - last_frames) * 1e9 / (double)armTicksToNs(tick - last_tick) : 0.0;
  last_tick = tick;
  last_frames = frames;
  debugPrintf("[game] %lu frames (%.1f fps), %u audio blocks, %d Java objects\n", frames, fps,
              (unsigned)dtm_audio_blocks(), jni_live_objects());
  dcr_boost_report();
}

/* -------------------------------------------- GameManager.SystemInit */
/* A string argument of a native: a local reference, released after the call
 * (the engine copies what it keeps). */
static void system_init(void) {
  debugPrintf("[game] NativeGameLib.InitDeviceProperties\n");
  g_n.InitDeviceProperties(ENV, CLS);

  JObj *apk = jni_str(DCR_ANDROID_APK);
  JObj *files = jni_str(DCR_ANDROID_FILES "/");
  JObj *cache = jni_str(DCR_ANDROID_CACHE "/");
  JObj *external = jni_str("/storage/emulated/0/");
  debugPrintf("[game] NativeGameLib.InitFileManager(%s -> %s)\n", DCR_ANDROID_APK, dcr_apk_path());
  g_n.InitFileManager(ENV, CLS, apk, files, cache, external, 0);
  jni_release(apk);
  jni_release(files);
  jni_release(cache);
  jni_release(external);

  int opensl = 0;
  if (g_n.InitOpenSLSoundManager)
    opensl = g_n.InitOpenSLSoundManager(ENV, CLS, jni_singleton("android/content/res/AssetManager"));
  debugPrintf("[game] NativeGameLib.InitOpenSLSoundManager -> %d\n", opensl);
  if (!opensl) {
    debugPrintf("[game] NativeGameLib.InitJavaSoundManager\n");
    g_n.InitJavaSoundManager(ENV, CLS);
  }

  JObj *lang = jni_str("en"); /* Locale.getDefault().getLanguage() */
  debugPrintf("[game] NativeGameLib.SystemInit(%d, %d, en)\n", g_w, g_h);
  g_n.SystemInit(ENV, CLS, g_w, g_h, lang);
  jni_release(lang);
}

/* ----------------------------------------------------------------- run */
int dtm_game_run(void) {
  egl_up();
  dtm_audio_init();
  dtm_input_init();
  dcr_watchdog_start();
  rt_watchdog_add_counter("audio blocks", dtm_audio_blocks);

  system_init();
  debugPrintf("[game] NativeGameLib.GameInit\n");
  g_n.GameInit(ENV, CLS);
  g_engine_up = 1;
  debugPrintf("[game] GameInit done: the engine is up\n");
  log_flush_ring();

  /* ---- GameManager.Render, and the UI thread's input, in turn ---- */
  u64 last_report = armGetSystemTick();
  int first = 1;
  unsigned long quiet_at = 0;
  while (!g_exit && !rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll(); /* focus, freezes: port_focus_lost/gained, port_process_frozen */
    if (!rt_focused()) {
      svcSleepThread(50000000ll);
      continue;
    }
    if (g_resuming) { /* the renderer's mIsResuming: no input, no step, until the engine is back */
      if (!g_n.onResumeStep || !g_n.onResumeStep(ENV, CLS))
        g_resuming = 0;
    } else {
      dtm_input_poll(g_w, g_h);
      if (!g_n.step(ENV, CLS)) {
        debugPrintf("[game] step() returned false: the game closes (shutdownApp)\n");
        g_exit = 1;
      } else if (g_n.gameRequestedQuit && g_n.gameRequestedQuit(ENV, CLS)) {
        /* the Java asks "quit?" in a dialog; here Back on the title screen
         * closes the game at once */
        debugPrintf("[game] the game asked to quit: confirmed\n");
        if (g_n.confirmQuitRequest)
          g_n.confirmQuitRequest(ENV, CLS, 1);
      } else if (g_n.gameRequestedRestart && g_n.gameRequestedRestart(ENV, CLS)) {
        debugPrintf("[game] the game asked to restart: not done here, it goes on\n");
      }
    }
    b_eglSwapBuffers(g_dpy, g_surf);

    const u64 now = armGetSystemTick();
    const unsigned long frames = (unsigned long)dcr_gl_frames();
    if (first) {
      first = 0;
      dcr_boost_launch_end();
      debugPrintf("[game] first frame presented\n");
      quiet_at = frames + 180;
    }
    /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
     * written out every 10 s and by the watchdog. */
    if (quiet_at && frames >= quiet_at) {
      quiet_at = 0;
      log_set_quiet(1);
    }
    if (armTicksToNs(now - last_report) >= 10000000000ull) {
      last_report = now;
      report();
      log_flush_ring();
    }
  }

  /* ---- onPause, onStop, onDestroy ---- */
  debugPrintf("[game] leaving (%s)\n", g_exit ? "the game closed itself" : "closed from the system");
  log_set_quiet(0);
  rt_applet_stop();
  if (rt_focused()) {
    g_n.onPause(ENV, CLS);
    if (g_n.saveOnExit)
      g_n.saveOnExit(ENV, CLS);
  }
  dtm_audio_shutdown();
  egl_down();
  debugPrintf("[game] closed\n");
  log_flush_ring();
  return 0;
}
