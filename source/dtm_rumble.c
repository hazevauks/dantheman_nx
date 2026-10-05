/* dtm_rumble.c -- the controller rumbles when the game shakes its camera.
 *
 * The game never asks Android to vibrate: there is nothing of the phone's to
 * pass on. What it has is a camera shake, GameCamera::Shake(amount,
 * seconds), called when the player or an enemy is hit, when something
 * breaks, and for the bosses' quakes: the moments a controller should be
 * felt. So that function is replaced by one that does what it did and then
 * rumbles player 1's controller, stronger for a bigger shake, for as long as
 * the shake lasts.
 *
 * What it did, read from this build's code (libmortargame.so of Dan the Man
 * 1.2.1): unless either value is zero (MathUtils::IsZero, against the
 * engine's own epsilon), the seconds go to the camera's +0x58 and +0x60 and
 * the amount to +0x5c. Another build may differ, so the function is replaced
 * only when its code is the code that was read; otherwise it is left alone
 * and nothing rumbles. [controls] rumble = false in config.ini leaves it
 * alone as well. MIT.
 */
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dtm.h"
#include "so_util.h"
#include "util.h"

/* GameCamera, 1.2.1 */
#define CAM_SHAKE_LEFT 0x58   /* float: seconds left */
#define CAM_SHAKE_AMOUNT 0x5c /* float */
#define CAM_SHAKE_TOTAL 0x60  /* float: seconds, as asked */
/* where GameCamera::Shake finds the epsilon: a pointer to it, at this offset
 * of the module (its literal at +0x50 added to the pc of +0x12) */
#define EPSILON_SLOT 0xc8c3c8
#define SHAKE_LITERAL_AT 0x50
#define SHAKE_LITERAL 0x007bfe6eu

/* the longest one shake rumbles, and the amount that is full strength */
#define RUMBLE_MAX_MS 600
#define SHAKE_FULL 10.0f

static int (*g_is_zero)(float value, float epsilon);
static const float *g_epsilon;
static volatile u64 g_until; /* the tick the rumble ends at; 0: none */
static volatile float g_strength;
static volatile int g_pending;

/* GameCamera::Shake(float amount, float seconds). The engine's threads may
 * call it: the controller is only driven from the frame loop
 * (dtm_rumble_frame). */
static void camera_shake(uint8_t *camera, float amount, float seconds) {
  if (g_is_zero(amount, *g_epsilon) || g_is_zero(seconds, *g_epsilon))
    return;
  memcpy(camera + CAM_SHAKE_LEFT, &seconds, sizeof seconds);
  memcpy(camera + CAM_SHAKE_AMOUNT, &amount, sizeof amount);
  memcpy(camera + CAM_SHAKE_TOTAL, &seconds, sizeof seconds);

  static int logged;
  if (logged < 8) {
    logged++;
    debugPrintf("[rumble] camera shake %.2f for %.2f s\n", (double)amount, (double)seconds);
  }
  float a = amount < 0 ? -amount : amount;
  float s = a / SHAKE_FULL;
  s = s < 0.25f ? 0.25f : s > 1.0f ? 1.0f : s;
  u64 ms = seconds > 0 ? (u64)(seconds * 1000.0f) : 0;
  if (ms > RUMBLE_MAX_MS)
    ms = RUMBLE_MAX_MS;
  if (ms < 60)
    ms = 60;
  g_strength = s;
  g_until = armGetSystemTick() + armNsToTicks(ms * 1000000ull);
  g_pending = 1;
}

/* ------------------------------------------------------------ the controller */
/* The rumble handles of player 1's controller, by its style, made once. */
static int handles(HidVibrationDeviceHandle *h, int *n) {
  static const u32 tags[5] = {HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadFullKey,
                              HidNpadStyleTag_NpadJoyDual, HidNpadStyleTag_NpadJoyLeft,
                              HidNpadStyleTag_NpadJoyRight};
  static HidVibrationDeviceHandle cache[5][2];
  static int made[5];
  HidNpadIdType id;
  const u64 style = dtm_input_controller(0, &id);
  int k = -1;
  for (int i = 0; i < 5 && k < 0; i++)
    if (style & tags[i])
      k = i;
  if (k < 0)
    return 0;
  const int count = k >= 3 ? 1 : 2;
  if (!made[k]) {
    static const char *const names[5] = {"the console's Joy-Cons", "a Pro Controller", "a Joy-Con pair",
                                         "a left Joy-Con", "a right Joy-Con"};
    const Result rc = hidInitializeVibrationDevices(cache[k], count, id, tags[k]);
    made[k] = R_SUCCEEDED(rc) ? 1 : -1;
    bool permitted = true;
    const Result prc = hidIsVibrationPermitted(&permitted);
    debugPrintf("[rumble] %s (hid id %d): %d motor(s), set up 0x%x; the console's vibration setting is %s\n",
                names[k], (int)id, count, (unsigned)rc,
                R_FAILED(prc) ? "unknown" : permitted ? "on" : "OFF (System Settings > Controllers and Sensors)");
  }
  if (made[k] < 0)
    return 0;
  memcpy(h, cache[k], sizeof(HidVibrationDeviceHandle) * (size_t)count);
  *n = count;
  return 1;
}

static void drive(float strength) {
  HidVibrationDeviceHandle h[2];
  int n = 0;
  if (!handles(h, &n)) {
    static int told;
    if (!told++)
      debugPrintf("[rumble] no controller of player 1 that can rumble\n");
    return;
  }
  HidVibrationValue v[2];
  for (int i = 0; i < 2; i++) {
    v[i].amp_low = strength;
    v[i].freq_low = 160.0f;
    v[i].amp_high = strength * 0.6f;
    v[i].freq_high = 320.0f;
  }
  const Result rc = hidSendVibrationValues(h, v, n);
  static int told;
  if (strength > 0 && told < 3) {
    told++;
    debugPrintf("[rumble] strength %.2f sent to %d motor(s): 0x%x\n", (double)strength, n, (unsigned)rc);
  }
}

static int g_on;

/* Every frame: a new shake starts the rumble, the time ends it. */
void dtm_rumble_frame(void) {
  if (g_pending) {
    g_pending = 0;
    g_on = 1;
    drive(g_strength);
  }
  if (g_on && armGetSystemTick() >= g_until) {
    g_on = 0;
    drive(0.0f);
  }
}

/* The game leaves the screen or closes: the controller is still. */
void dtm_rumble_stop(void) {
  g_pending = 0;
  if (g_on) {
    g_on = 0;
    drive(0.0f);
  }
}

/* After the module is sealed as code, before any of it runs. */
void dtm_rumble_patch(void) {
  if (!dcr_config()->rumble) {
    debugPrintf("[rumble] off (config.ini)\n");
    return;
  }
  const uintptr_t fn = so_try_find_addr_rx(&g_mod_game, "_ZN10GameCamera5ShakeEff");
  const uintptr_t is_zero = so_try_find_addr_rx(&g_mod_game, "_ZN9MathUtils6IsZeroEff");
  uint32_t literal = 0;
  if (fn & 1)
    memcpy(&literal, (const uint8_t *)(fn & ~(uintptr_t)1) + SHAKE_LITERAL_AT, sizeof literal);
  if (!fn || !is_zero || literal != SHAKE_LITERAL) {
    debugPrintf("[rumble] GameCamera::Shake is not the 1.2.1 code this port knows: no rumble\n");
    return;
  }
  const float *const *slot = (const float *const *)((uintptr_t)g_mod_game.load_virtbase + EPSILON_SLOT);
  g_epsilon = *slot;
  g_is_zero = (int (*)(float, float))is_zero;
  if (!g_epsilon || dtm_hook(fn, camera_shake) != 0) {
    debugPrintf("[rumble] could not patch GameCamera::Shake: no rumble\n");
    return;
  }
  debugPrintf("[rumble] the controller rumbles with the game's camera shakes (epsilon %g)\n",
              (double)*g_epsilon);
}
