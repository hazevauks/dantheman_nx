/* dtm_input.c -- the controllers and the touch screen, as the game's Java
 * handed them to the engine.
 *
 * On Android:
 *   keys      MortarGameView.onKeyDown / onKeyUp queue the KeyEvent, and the
 *             renderer hands each to NativeGameLib.keyEvent(keyCode, down,
 *             flag, deviceId) before the frame's step
 *   gamepads  AndroidGameControllerManager reports each one with
 *             onGameControllerAttach(deviceId, name) / Detach(deviceId);
 *             their buttons arrive as keys (KEYCODE_BUTTON_*, DPAD_*)
 *   touch     MultiTouchInputHandler: NativeGameLib.touchEvent(action, time,
 *             pointer, x / width, y / height, pressure, size), one call per
 *             finger, the position as a fraction of the screen
 *
 * Here one thread does it all, before the frame's step (dtm_game.c). The
 * Joy-Cons on the console and player 1's controller are one Android device;
 * player 2's is another. The sticks' analogue values (the Java's
 * NativeGameLib.motionEvent) are not sent yet: the left stick works as the
 * D-pad ([controls] left_stick_as_dpad). MIT.
 */
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dtm.h"
#include "rt_pad.h"
#include "util.h"

#define ENV g_jni_env
#define CLS g_gamelib

/* android.view.KeyEvent */
enum {
  AK_BACK = 4,
  AK_DPAD_UP = 19,
  AK_DPAD_DOWN = 20,
  AK_DPAD_LEFT = 21,
  AK_DPAD_RIGHT = 22,
  AK_BUTTON_A = 96,
  AK_BUTTON_B = 97,
  AK_BUTTON_X = 99,
  AK_BUTTON_Y = 100,
  AK_BUTTON_L1 = 102,
  AK_BUTTON_R1 = 103,
  AK_BUTTON_L2 = 104,
  AK_BUTTON_R2 = 105,
  AK_BUTTON_THUMBL = 106,
  AK_BUTTON_THUMBR = 107,
  AK_BUTTON_START = 108,
};
/* android.view.MotionEvent */
enum { AM_DOWN = 0, AM_UP = 1, AM_MOVE = 2 };

static const struct {
  u64 button;
  int code;
} k_keys[] = {
    {HidNpadButton_A, AK_BUTTON_A},          {HidNpadButton_B, AK_BUTTON_B},
    {HidNpadButton_X, AK_BUTTON_X},          {HidNpadButton_Y, AK_BUTTON_Y},
    {HidNpadButton_L, AK_BUTTON_L1},         {HidNpadButton_R, AK_BUTTON_R1},
    {HidNpadButton_ZL, AK_BUTTON_L2},        {HidNpadButton_ZR, AK_BUTTON_R2},
    {HidNpadButton_StickL, AK_BUTTON_THUMBL}, {HidNpadButton_StickR, AK_BUTTON_THUMBR},
    {HidNpadButton_Plus, AK_BUTTON_START},   {HidNpadButton_Minus, AK_BACK},
    {HidNpadButton_Up, AK_DPAD_UP},          {HidNpadButton_Down, AK_DPAD_DOWN},
    {HidNpadButton_Left, AK_DPAD_LEFT},      {HidNpadButton_Right, AK_DPAD_RIGHT},
};
#define NKEYS (sizeof k_keys / sizeof k_keys[0])

/* The left stick as D-pad keys: past this much of its travel. */
#define STICK_ON 0.5f

/* Players: 0 is the console's Joy-Cons and player 1's controller together,
 * 1 is player 2's. Their Android device ids are 1 and 2. */
#define PLAYERS RT_PAD_MAX_PLAYERS
typedef struct {
  PadState pads[2]; /* player 1: its slot and the handheld; the others: one */
  int npads;
  u64 held;         /* this player's buttons last frame, the stick's D-pad in */
  int attached;
} Player;
static Player g_players[PLAYERS];

/* Touch: the fingers down last frame, by the system's finger id. */
#define MAX_TOUCH 10
static struct {
  u32 id;
  float x, y;
} g_touch[MAX_TOUCH];
static int g_ntouch;

static jlong now_ms(void) { return (jlong)(armTicksToNs(armGetSystemTick()) / 1000000ull); }

void dtm_input_init(void) {
  rt_pad_setup(PLAYERS, 1);
  memset(g_players, 0, sizeof g_players);
  for (int p = 0; p < PLAYERS; p++) {
    rt_pad_slot(&g_players[p].pads[0], p);
    g_players[p].npads = 1;
  }
  rt_pad_slot(&g_players[0].pads[1], RT_PAD_HANDHELD);
  g_players[0].npads = 2;
  hidInitializeTouchScreen();
  debugPrintf("[input] %d player(s), the touch screen %s\n", PLAYERS, dcr_config()->touch ? "on" : "off");
}

static void key(int device, int code, int down) {
  if (dcr_config()->log_input)
    debugPrintf("[input] key %s %d from device %d\n", down ? "down" : "up", code, device);
  g_n.keyEvent(ENV, CLS, code, down ? 1 : 0, 0, device);
}

static void poll_player(int p) {
  Player *pl = &g_players[p];
  const int device = p + 1;
  u64 buttons = 0;
  int connected = 0;
  float lx = 0, ly = 0;
  for (int i = 0; i < pl->npads; i++) {
    padUpdate(&pl->pads[i]);
    if (!padIsConnected(&pl->pads[i]))
      continue;
    connected = 1;
    float sticks[4];
    buttons |= rt_pad_read(&pl->pads[i], sticks);
    if (sticks[0] * sticks[0] + sticks[1] * sticks[1] > lx * lx + ly * ly)
      lx = sticks[0], ly = sticks[1];
  }
  if (connected != pl->attached) {
    pl->attached = connected;
    debugPrintf("[input] controller of player %d %s\n", device, connected ? "attached" : "detached");
    if (connected && g_n.onGameControllerAttach) {
      JObj *name = jni_str("Nintendo Switch Controller");
      g_n.onGameControllerAttach(ENV, CLS, device, name);
      jni_release(name);
    } else if (!connected && g_n.onGameControllerDetach) {
      g_n.onGameControllerDetach(ENV, CLS, device);
    }
  }
  if (dcr_config()->swap_ab) {
    const u64 ab = buttons & (HidNpadButton_A | HidNpadButton_B);
    if (ab == HidNpadButton_A || ab == HidNpadButton_B)
      buttons ^= HidNpadButton_A | HidNpadButton_B;
  }
  if (dcr_config()->stick_dpad) {
    if (lx <= -STICK_ON) buttons |= HidNpadButton_Left;
    if (lx >= STICK_ON) buttons |= HidNpadButton_Right;
    if (ly >= STICK_ON) buttons |= HidNpadButton_Up;
    if (ly <= -STICK_ON) buttons |= HidNpadButton_Down;
  }
  const u64 changed = buttons ^ pl->held;
  for (unsigned i = 0; i < NKEYS && changed; i++)
    if (changed & k_keys[i].button)
      key(device, k_keys[i].code, (buttons & k_keys[i].button) != 0);
  pl->held = buttons;
}

static void touch(int action, u32 id, float x, float y) {
  if (dcr_config()->log_input && action != AM_MOVE)
    debugPrintf("[input] touch %s finger %u at %.3f,%.3f\n", action == AM_DOWN ? "down" : "up",
                (unsigned)id, (double)x, (double)y);
  g_n.touchEvent(ENV, CLS, action, now_ms(), (jint)id, x, y, 1.0f, 0.0f);
}

/* The touch screen reports in 1280x720 whatever the rendering size: the
 * engine takes fractions of the screen. */
static void poll_touch(void) {
  HidTouchScreenState ts = {0};
  if (!hidGetTouchScreenStates(&ts, 1))
    return;
  int n = ts.count < MAX_TOUCH ? ts.count : MAX_TOUCH;
  /* lifted: down last frame, gone now */
  for (int i = 0; i < g_ntouch; i++) {
    int still = 0;
    for (int j = 0; j < n && !still; j++)
      still = ts.touches[j].finger_id == g_touch[i].id;
    if (!still)
      touch(AM_UP, g_touch[i].id, g_touch[i].x, g_touch[i].y);
  }
  /* new or moved */
  typeof(g_touch[0]) now[MAX_TOUCH];
  for (int j = 0; j < n; j++) {
    now[j].id = ts.touches[j].finger_id;
    now[j].x = (float)ts.touches[j].x / 1280.0f;
    now[j].y = (float)ts.touches[j].y / 720.0f;
    int was = -1;
    for (int i = 0; i < g_ntouch && was < 0; i++)
      if (g_touch[i].id == now[j].id)
        was = i;
    if (was < 0)
      touch(AM_DOWN, now[j].id, now[j].x, now[j].y);
    else if (g_touch[was].x != now[j].x || g_touch[was].y != now[j].y)
      touch(AM_MOVE, now[j].id, now[j].x, now[j].y);
  }
  memcpy(g_touch, now, sizeof now[0] * (size_t)n);
  g_ntouch = n;
}

void dtm_input_poll(int width, int height) {
  (void)width, (void)height;
  for (int p = 0; p < PLAYERS; p++)
    poll_player(p);
  if (dcr_config()->touch)
    poll_touch();
}

/* Focus lost: Android sends held keys and touches as cancelled. */
void dtm_input_reset(void) {
  for (int p = 0; p < PLAYERS; p++) {
    Player *pl = &g_players[p];
    for (unsigned i = 0; i < NKEYS; i++)
      if (pl->held & k_keys[i].button)
        key(p + 1, k_keys[i].code, 0);
    pl->held = 0;
  }
  for (int i = 0; i < g_ntouch; i++)
    touch(AM_UP, g_touch[i].id, g_touch[i].x, g_touch[i].y);
  g_ntouch = 0;
}
