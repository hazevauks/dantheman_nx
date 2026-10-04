/* dcr_config.c -- Dan the Man's settings: config.ini's options, on the
 * runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * Written whole on the first start, the options a newer build adds appended
 * at the end ("# Added by build ..."), [config] version = 1 last. Never
 * rename or reorder an option once players have it: their config.ini files
 * must read the same. Booleans take true/false, yes/no, on/off, 1/0. Read
 * once at start-up: changes apply the next time the game starts. MIT.
 */
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {
    .stick_dpad = 1,
    .swap_ab = 0,
    .touch = 1,
    .res_w = 1280,
    .res_h = 720,
    .boost = 1,
    .language = "auto",
    .events = 1,
    .rumble = 1,
    .background_saves = 1,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

static const CfgOpt k_opts[] = {
    {"controls", "left_stick_as_dpad", "true",
     "The left stick also works as the D-pad (moving, and the menus), as\n"
     "# Android turns a gamepad's stick into D-pad keys.",
     CFG_BOOL, NULL, &g_cfg.stick_dpad},
    CFG_ROW_SWAP_AB("Swap A and B. false: the buttons work by where they are, as on the\n"
                    "# game's own pad: B (bottom) jumps and confirms, Y (left) hits, A (right)\n"
                    "# uses the secondary weapon, X (top) switches weapons.",
                    &g_cfg.swap_ab),
    {"touch", "enabled", "true", "The touch screen works as on the phone (handheld mode).", CFG_BOOL,
     NULL, &g_cfg.touch},
    CFG_ROW_RESOLUTION("auto",
                       "Rendering resolution: 720, 1080 or auto (1080 if docked when the game\n"
                       "# starts)."),
    CFG_ROW_BOOST("CPU at 1785 MHz while the game starts (until its first picture).", &g_cfg.boost),
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG("Show the start-up log on screen at every launch. Off: the log appears only\n"
                     "# while something is being set up (first launch, a new APK or NRO).",
                     &g_cfg.boot_log),
    CFG_ROW_LOG_JNI("Write every Java method the game calls to debug.log (for bug reports).",
                    &g_cfg.log_jni),
    {"debug", "log_input", "false",
     "Write every key and touch the game receives to debug.log (for bug reports).", CFG_BOOL, NULL,
     &g_cfg.log_input},
    {"game", "language", "auto",
     "The game's language. auto: the console's. Or a code: en, es, es-419, de, fr,\n"
     "# it, ja, pt, ru, tr, zh-CN, zh-TW (a language the game does not have plays\n"
     "# in English).",
     CFG_TEXT, NULL, g_cfg.language, 0, 0, sizeof g_cfg.language},
    {"game", "events", "true",
     "Weekly events. The game only trusts a date from its own servers, which a\n"
     "# Switch does not reach; true: it takes the console's clock instead, so the\n"
     "# day's event can be played (and whatever else the game times runs on that\n"
     "# clock). false: as without a connection, no events.",
     CFG_BOOL, NULL, &g_cfg.events},
    {"controls", "rumble", "true",
     "Player 1's controller rumbles when the game shakes the screen (a hit, a\n"
     "# boss's quake). The game itself has no vibration: this is the port's.",
     CFG_BOOL, NULL, &g_cfg.rumble},
    {"performance", "background_saves", "true",
     "The game's saves are written to the SD card by a thread of the port's, so\n"
     "# the game does not stop for them. false: written inside the game's frame,\n"
     "# as on Android (try this if a save is ever lost).",
     CFG_BOOL, NULL, &g_cfg.background_saves},
    /* [config] version = 1: the engine's row, last (CfgTable.version) */
};

static void apply(void) {
  const RtConfig *rt = rt_config(); /* the resolution: rt_cfg.c sets the window to it */
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;
  const int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] %dx%d (%s, %s); stick as D-pad %s, A/B %s, touch %s, CPU boost %s\n",
              g_cfg.res_w, g_cfg.res_h, rt_config_get("display", "resolution"),
              docked ? "docked" : "handheld", g_cfg.stick_dpad ? "on" : "off",
              g_cfg.swap_ab ? "swapped" : "by position (B jumps, Y hits)", g_cfg.touch ? "on" : "off",
              g_cfg.boost ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
