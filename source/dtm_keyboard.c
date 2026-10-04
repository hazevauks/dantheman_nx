/* dtm_keyboard.c -- com.halfbrick.mortar.SoftKeyboard, as the Switch's own
 * keyboard.
 *
 * The game has one text field: the name of the custom character
 * (GameScreenPlayerCustom). On Android the engine asks for the keyboard with
 * SoftKeyboard.ShowKeyboard(text, ...), and the Java's hidden editor reports
 * back through three natives of NativeGameLib, registered from JNI_OnLoad:
 *
 *   native_keyboardUpdateText(text, selStart, selEnd, composeStart,
 *     composeEnd)   after every change: the whole text, where the cursor is
 *                   and what part is still being composed (-1, -1: none)
 *   native_keyboardProcessDone()        the keyboard's Done key
 *   native_keyboardProcessCancelled()   the keyboard dismissed
 *
 * Here ShowKeyboard only notes the request (it comes in the middle of the
 * engine's frame); the frame loop then opens the system keyboard with the
 * field's text, which holds the game until the player is done, and reports
 * the result the way the Java would have: the new text with the cursor at
 * its end and Done, or Cancelled. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dtm.h"
#include "rt_applet.h"
#include "util.h"

#define GL "com/halfbrick/mortar/NativeGameLib"
#define NAME_MAX_CHARS 24 /* what the keyboard lets in */

static char g_text[256];
static volatile int g_wanted;

/* static void ShowKeyboard(String text, int, int, int, int) */
JNI_H_DECL(dtm_h_keyboard_show) {
  snprintf(g_text, sizeof g_text, "%s", jni_utf(a[0].l));
  g_wanted = 1;
  return jv_none();
}

/* static void HideKeyboard(): one asked for and not yet shown is dropped */
JNI_H_DECL(dtm_h_keyboard_hide) {
  g_wanted = 0;
  return jv_none();
}

/* Java's String.length(): UTF-16 units of a UTF-8 text. */
static int utf16_length(const char *s) {
  int n = 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    if ((*p & 0xc0) != 0x80)
      n += *p >= 0xf0 ? 2 : 1;
  return n;
}

/* After the engine's frame: the keyboard, if one was asked for. */
void dtm_keyboard_frame(void) {
  if (!g_wanted)
    return;
  g_wanted = 0;
  void (*update)(void *env, void *cls, void *text, jint, jint, jint, jint) =
      (void (*)(void *, void *, void *, jint, jint, jint, jint))jni_native(GL, "native_keyboardUpdateText");
  jboolean (*done)(void *env, void *cls) = (jboolean (*)(void *, void *))jni_native(GL, "native_keyboardProcessDone");
  void (*cancelled)(void *env, void *cls) =
      (void (*)(void *, void *))jni_native(GL, "native_keyboardProcessCancelled");
  if (!update || !done || !cancelled) {
    debugPrintf("[keyboard] the engine's keyboard natives are not registered: no keyboard\n");
    return;
  }

  char out[256] = {0};
  SwkbdConfig kbd;
  Result rc = swkbdCreate(&kbd, 0);
  if (R_SUCCEEDED(rc)) {
    swkbdConfigMakePresetDefault(&kbd);
    if (g_text[0])
      swkbdConfigSetInitialText(&kbd, g_text);
    swkbdConfigSetStringLenMax(&kbd, NAME_MAX_CHARS);
    dtm_input_reset();   /* what is held now ends on the other screen */
    dtm_audio_pause(1);
    dcr_applet_busy(1);  /* a system screen holds this thread: no frames, and nothing wrong */
    rc = swkbdShow(&kbd, out, sizeof out);
    dcr_applet_busy(0);
    dtm_audio_pause(0);
    swkbdClose(&kbd);
  }
  if (R_FAILED(rc)) {
    debugPrintf("[keyboard] cancelled (0x%x)\n", (unsigned)rc);
    cancelled(g_jni_env, g_gamelib);
    return;
  }
  const int end = utf16_length(out);
  debugPrintf("[keyboard] entered %d character(s)\n", end);
  JObj *text = jni_str(out);
  update(g_jni_env, g_gamelib, text, end, end, -1, -1);
  jni_release(text);
  done(g_jni_env, g_gamelib);
}
