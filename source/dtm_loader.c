/* dtm_loader.c -- loading Dan the Man's one module, libmortargame.so.
 *
 * The APK's lib/armeabi-v7a holds four libraries; only this one is the game:
 * Halfbrick's Mortar engine with the game and gnustl linked in, Thumb-2 and
 * GLES 2 (libjs.so and libadcolony.so are AdColony's, libcrashlytics.so is
 * the crash reporter's: none is loaded). Its DT_NEEDED are system libraries
 * only (libc, libm, liblog, libGLESv2, libandroid, libdl), all served by the
 * shims: 297 imports, 73 gl* through the GL layer, the rest from the import
 * table (runtime/tools/gen_imports.py). Nothing in it writes code at run
 * time, so the module is mapped the plain way: staged, relocated, resolved,
 * then sealed as code (RX text, RW data) before anything in it runs. Its
 * init array has 1006 constructors.
 *
 * Its natives: most of NativeGameLib's are exported by name
 * (Java_com_halfbrick_mortar_NativeGameLib_native_1*); the ones that are not
 * are registered from JNI_OnLoad (RegisterNatives), so each is looked up by
 * name first, then among the registered ones. MIT.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "codespace.h"
#include "config.h"
#include "dcr_path.h"
#include "dtm.h"
#include "error.h"
#include "imports.h"
#include "so_util.h"
#include "util.h"

so_module g_mod_game;
DtmNatives g_n;

/* ---------------------------------------------------------------- natives */
#define JNI_ "Java_com_halfbrick_mortar_NativeGameLib_native_1"
#define GAMELIB "com/halfbrick/mortar/NativeGameLib"

static const struct {
  const char *name;
  size_t off;
  int required;
} k_natives[] = {
#define NAT(n, req) {#n, offsetof(DtmNatives, n), req}
    NAT(InitDeviceProperties, 1), NAT(InitFileManager, 1),      NAT(InitOpenSLSoundManager, 0),
    NAT(InitJavaSoundManager, 1), NAT(GLESVersion, 0),          NAT(SystemInit, 1),
    NAT(GameInit, 1),             NAT(step, 1),                 NAT(gameRequestedQuit, 0),
    NAT(gameRequestedRestart, 0), NAT(confirmQuitRequest, 0),   NAT(saveOnExit, 0),
    NAT(onPause, 1),              NAT(onResume, 1),             NAT(onResumeStep, 0),
    NAT(onFocusLost, 0),          NAT(onFocusRetrieved, 0),     NAT(SetAppLicensed, 0),
    NAT(StoragePermissionResult, 0), NAT(keyEvent, 1),          NAT(motionEvent, 0),
    NAT(touchEvent, 1),           NAT(onGameControllerAttach, 0), NAT(onGameControllerDetach, 0),
#undef NAT
};

/* After JNI_OnLoad: the exported ones by name, the others as registered. */
static int bind_natives(void) {
  int missing = 0;
  char sym[112];
  for (unsigned i = 0; i < sizeof k_natives / sizeof k_natives[0]; i++) {
    snprintf(sym, sizeof sym, JNI_ "%s", k_natives[i].name);
    uintptr_t a = so_try_find_addr_rx(&g_mod_game, sym);
    if (!a) {
      snprintf(sym, sizeof sym, "native_%s", k_natives[i].name);
      a = (uintptr_t)jni_native(GAMELIB, sym);
    }
    memcpy((uint8_t *)&g_n + k_natives[i].off, &a, sizeof a); /* a function pointer's slot */
    if (!a) {
      debugPrintf("[boot] %s native %s%s\n", k_natives[i].required ? "MISSING" : "no",
                  k_natives[i].name, k_natives[i].required ? "" : " (optional)");
      missing += k_natives[i].required;
    }
  }
  return missing;
}

/* -------------------------------------------------------------- loading */
int dtm_load_engine(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), DTM_LIB);
  int rc = so_load(&g_mod_game, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB  staged %p -> %p  (%d unresolved imports)\n", g_mod_game.base_name,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_base, g_mod_game.load_virtbase,
              missing);
  /* libgcc's __sync_* on ARM Linux call the kernel's user helpers through
   * literal pools: any such literal is pointed at the runtime's kuser.S. */
  so_fix_kuser_helpers(&g_mod_game);
  so_finalize(&g_mod_game);
  so_flush_caches(&g_mod_game);
  return 0;
}

/* Android runs a library's constructors inside System.loadLibrary, which
 * NativeGameLib.TryLoadGameLibrary calls before the first native; JNI_OnLoad
 * follows, where the engine keeps the VM and registers its other natives. */
void dtm_run_constructors(void) {
  const u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] %s constructors done in %llu ms\n", DTM_LIB,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  typedef jint (*fn_onload)(void *vm, void *reserved);
  fn_onload onload = (fn_onload)so_try_find_addr_rx(&g_mod_game, "JNI_OnLoad");
  if (onload)
    debugPrintf("[boot] JNI_OnLoad -> 0x%lx\n", (unsigned long)onload(g_jni_vm, NULL));
  if (bind_natives())
    fatal_error(DTM_LIB " is not the Dan the Man engine this port knows\n"
                "(natives are missing: see debug.log). The port is made for\n" PORT_APK_DESC ".");
}
