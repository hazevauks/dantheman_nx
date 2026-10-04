/* port_config.h -- Dan the Man's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Dan the Man"
#define PORT_NAME     "dantheman_nx"
#define PORT_PACKAGE  "com.halfbrick.dantheman"
#define PORT_BANNER   "dantheman_nx: Dan the Man (Halfbrick's Mortar engine, armeabi-v7a)"
/* libmortargame.so maps 0xcf87b4 bytes (~13 MB): the runtime's 32 MB default
 * region holds it. */

/* The APK, by what is in it (any file name): it holds the engine; of
 * several, the one that is this game at the version the port is made for. */
#define PORT_APK_DESC "Dan the Man 1.2.1 (com.halfbrick.dantheman, armeabi-v7a)"
#define PORT_APK_ROLES                                                                       \
  {.what = "the game", .need = (const char *const[]){"lib/armeabi-v7a/libmortargame.so", NULL}, \
   .package = "com.halfbrick.dantheman", .version_code = 1210006, .flags = RT_APK_PACKAGE_BONUS}
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's engine from the APK)"

/* ------------------------------------------------------------------ sound */
/* OpenSL ES stays refused (the runtime's default): the engine then mixes
 * itself and hands PCM to its Java MortarAudioMixerOut, which dtm_audio.c
 * plays through audout. */

/* ------------------------------------------------------------------ input */
#define RT_PAD_MAX_PLAYERS 2 /* the game has a two-player mode */

#endif
