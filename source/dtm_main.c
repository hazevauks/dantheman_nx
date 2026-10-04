/* dtm_main.c -- Dan the Man's part of the boot: the runtime's main()
 * (runtime/source/main.c) does the rest -- the log, config.ini, the NRO
 * self-update, the APK found by what it holds, its package checked -- and
 * calls these.
 *
 * The first launch (runtime/source/dcr_setup.c, from the plan below):
 * libmortargame.so out of lib/armeabi-v7a/ (the engine and the game) and
 * classes.txt (the Java class names its classes*.dex define, which
 * jni_core.c answers FindClass with), made again whenever the APK changes
 * (.setup stamps, keys "libmortargame.so" and "classes.txt"). The game's
 * assets are read straight out of the APK: the engine opens it itself, by
 * the path NativeGameLib.InitFileManager hands it (dtm_game.c). The bar, in
 * permille of the first launch:
 *     0- 200  (the APK found and checked: the runtime's main())
 *   200- 850  libmortargame.so unpacked (by bytes written)
 *   850- 950  the Java class list
 *        1000 the game starts
 * MIT.
 */
#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "dtm.h"
#include "error.h"
#include "rt_boot.h"
#include "util.h"

static const char *const k_libs[] = {DTM_LIB};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game's engine",
    .apk_requirement = "This port needs Dan the Man (com.halfbrick.dantheman) for\n"
                       "32-bit ARM (armeabi-v7a): use the APK of your own copy.",
    .libs_p0 = 200,
    .libs_p1 = 850,
    .classes_p0 = 850,
    .classes_p1 = 950,
};

/* From the APK to the game's first code: libmortargame.so and classes.txt
 * (again when the APK changed), then the engine loaded, relocated, resolved
 * against the shims and mapped as code. */
int port_load(const char *apk) {
  dcr_setup_from_apk(apk);
  if (dtm_load_engine() != 0)
    fatal_error("Could not load the game engine from %s/" DTM_LIB ".\n\n"
                "It is unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n" DTM_LIB
                " and .setup there to unpack it again. See debug.log.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary("mortargame"): the library's constructors and
 * JNI_OnLoad; then MortarGameActivity and its GLSurfaceView. */
void port_run(void) {
  dtm_java_init(); /* JNI_OnLoad needs the VM */
  dtm_run_constructors();
  dtm_game_run();
}

/* For the error screens. */
const char *port_apk_help(void) {
  return "Copy the APK of your own Dan the Man (com.halfbrick.dantheman,\n"
         "armeabi-v7a) into /switch/" PORT_NAME ". Any file name ending in .apk\n"
         "works: the game's code and assets are read from it.";
}
