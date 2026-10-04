/* dtm_time.c -- the weekly events, on the console's clock.
 *
 * The game's weekly events (and the other things it times: the story map's
 * wait before a level, the offers) are worked out from the date, from a
 * calendar inside the APK (definitions/weekly_events). But the game only
 * believes a date that comes from Halfbrick's time service: every frame
 * Game::UpdateServerTime asks Mortar's ITimeService for the time and whether
 * it is reliable, and keeps both in the Game object. With no server the
 * answer is "not reliable", and the events screen says there is no
 * connection (Game::IsServerTimeReliable, asked by the screen when it opens
 * and by its Play button).
 *
 * Nothing is fetched for an event: only the date is needed. So
 * Game::UpdateServerTime is replaced by a function that does what its
 * "reliable" branch did, with the console's clock as the time (the engine's
 * own Mortar::Timing::GetSecondsSinceEpoch, which it falls back to when
 * there is no time service at all).
 *
 * The fields it writes are the Game object's, at offsets read out of this
 * build's code (libmortargame.so of Dan the Man 1.2.1). Another build may
 * lay the object out differently, so the function is replaced only when its
 * code is the code those offsets were read from; otherwise it is left alone
 * and the events stay as they were. [game] events = false in config.ini
 * leaves it alone as well.
 *
 * The screen's other button, the video that the game offers there, also
 * asks for a network connection and an ad: neither exists here, and it
 * stays unavailable. MIT.
 */
#include <string.h>

#include "dcr_config.h"
#include "dtm.h"
#include "so_util.h"
#include "util.h"

/* The Game object, 1.2.1. */
#define GAME_SERVER_TIME 0x170    /* u64: seconds since the epoch */
#define GAME_UNRELIABLE_TIME 0x178 /* u64: zeroed while the time is reliable */
#define GAME_TIME_FRACTION 0x180  /* float: counts up to 1 with the frames */
#define GAME_TIME_RELIABLE 0x184  /* u8: what Game::IsServerTimeReliable returns */

static uint64_t (*g_epoch_seconds)(void);

/* Game::UpdateServerTime(float dt), its reliable branch. */
static void update_server_time(uint8_t *game, float dt) {
  const uint64_t now = g_epoch_seconds(), zero = 0;
  memcpy(game + GAME_SERVER_TIME, &now, sizeof now);
  memcpy(game + GAME_UNRELIABLE_TIME, &zero, sizeof zero);
  game[GAME_TIME_RELIABLE] = 1;
  float t;
  memcpy(&t, game + GAME_TIME_FRACTION, sizeof t);
  t += dt;
  t = t <= 0.0f ? 0.0f : t < 1.0f ? t : 1.0f;
  memcpy(game + GAME_TIME_FRACTION, &t, sizeof t);
}

/* The two instructions the offsets above were read from:
 *   +0x30  add.w  r1, r4, #0x170
 *   +0x34  strb.w r0, [r4, #0x184] */
static int is_known_code(uintptr_t fn) {
  static const uint8_t k_at_30[8] = {0x04, 0xf5, 0xb8, 0x71, 0x84, 0xf8, 0x84, 0x01};
  return (fn & 1) && !memcmp((const uint8_t *)(fn & ~(uintptr_t)1) + 0x30, k_at_30, sizeof k_at_30);
}

/* After the module is sealed as code, before any of it runs. */
void dtm_time_patch(void) {
  if (!dcr_config()->events) {
    debugPrintf("[time] weekly events off (config.ini): the game's own time service is asked\n");
    return;
  }
  const uintptr_t fn = so_try_find_addr_rx(&g_mod_game, "_ZN4Game16UpdateServerTimeEf");
  const uintptr_t epoch = so_try_find_addr_rx(&g_mod_game, "_ZN6Mortar6Timing20GetSecondsSinceEpochEv");
  if (!fn || !epoch || !is_known_code(fn)) {
    debugPrintf("[time] Game::UpdateServerTime is not the 1.2.1 code this port knows: "
                "the weekly events stay without a clock\n");
    return;
  }
  g_epoch_seconds = (uint64_t (*)(void))epoch;
  if (dtm_hook(fn, update_server_time) != 0) {
    debugPrintf("[time] could not patch Game::UpdateServerTime\n");
    return;
  }
  debugPrintf("[time] the game's server time is the console's clock: weekly events on\n");
}
