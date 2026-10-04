/* dtm_firebase.c -- the game without Google's Firebase.
 *
 * The engine carries the Firebase C++ SDK (app, analytics, remote config,
 * dynamic links, invites). Its start-up loads Java classes of its own from
 * dex files it carries inside the library, written to the cache folder and
 * opened with a DexClassLoader; when one cannot be loaded it logs "Java
 * class ... not found" and calls abort(). No Java runs here, and nothing
 * Firebase does has a use on a Switch.
 *
 * The game reaches all of it through one small namespace of its own,
 * FirebaseNS (Init, Update, the analytics events, the remote-config values,
 * the invite links), and the library exports those functions by name. So
 * each is replaced at its first instruction by a jump to a function here:
 * Firebase is never started, the events go nowhere, and a remote-config
 * value is the default the game itself handed to FirebaseNS::Init (what a
 * phone that never reached Google's servers would answer). The three
 * functions that only read or write FirebaseNS's own flags
 * (IsFetchActivated, ResetFetchActivated, ForceFetched) stay as they are.
 * MIT.
 */
#include <stdlib.h>
#include <string.h>

#include "dtm.h"
#include "so_util.h"
#include "util.h"

/* The engine is Thumb-2: LDR.W PC, [PC] and the address after it (the
 * literal must be 4-aligned, so a function at an address that is not gets a
 * NOP first). The address's low bit picks the state of the function jumped
 * to, as any interworking branch. An ARM function takes the runtime's stub. */
int dtm_hook(uintptr_t fn, void *dst) {
  if (!(fn & 1)) {
    hook_arm(fn, (uintptr_t)dst);
    return 0;
  }
  const uintptr_t at = fn & ~(uintptr_t)1;
  uint8_t stub[10];
  size_t n = 0;
  if (at & 2) {
    stub[n++] = 0x00; /* nop */
    stub[n++] = 0xbf;
  }
  stub[n++] = 0xdf; /* ldr.w pc, [pc] */
  stub[n++] = 0xf8;
  stub[n++] = 0x00;
  stub[n++] = 0xf0;
  const uint32_t target = (uint32_t)(uintptr_t)dst;
  memcpy(stub + n, &target, 4);
  n += 4;
  return so_patch_code((void *)at, stub, n);
}

/* ------------------------------------------------- remote-config defaults */
/* FirebaseNS::ConfigKeyValue, as firebase::remote_config::ConfigKeyValue:
 * two strings. The table is the caller's: copied. */
typedef struct {
  const char *key, *value;
} ConfigKeyValue;

static struct {
  char *key, *value;
} *g_defaults;
static unsigned g_ndefaults;

static int fb_init(void *env, void *activity, const ConfigKeyValue *defaults, unsigned count) {
  (void)env, (void)activity;
  debugPrintf("[firebase] FirebaseNS::Init: not started; %u remote-config default(s)\n", count);
  if (!defaults || !count || count > 4096 || g_defaults)
    return 0;
  g_defaults = calloc(count, sizeof *g_defaults);
  if (!g_defaults)
    return 0;
  for (unsigned i = 0; i < count; i++) {
    if (!defaults[i].key)
      break;
    g_defaults[i].key = strdup(defaults[i].key);
    g_defaults[i].value = strdup(defaults[i].value ? defaults[i].value : "");
    g_ndefaults = i + 1;
    if (i < 8)
      debugPrintf("[firebase]   %s = %.60s\n", g_defaults[i].key, g_defaults[i].value);
  }
  return 0;
}

/* GetConfigValue(key, out, size): the game's own default for it, or "". */
static int fb_config_value(const char *key, char *out, unsigned size) {
  if (!out || !size)
    return 0;
  out[0] = 0;
  for (unsigned i = 0; key && i < g_ndefaults; i++)
    if (!strcmp(g_defaults[i].key, key)) {
      strncpy(out, g_defaults[i].value, size - 1);
      out[size - 1] = 0;
      return 1;
    }
  static int told;
  if (told++ < 16)
    debugPrintf("[firebase] GetConfigValue(%s): no default\n", key ? key : "(null)");
  return 0;
}

/* GetDeepLinkValue(out, size), GetShorLinkResult(out): no link. */
static int fb_no_string(char *out) {
  if (out)
    out[0] = 0;
  return 0;
}

/* Everything else: nothing happens, 0 / false / void. */
static int fb_nothing(void) { return 0; }

/* ------------------------------------------------------------- the hooks */
static const struct {
  const char *sym;
  void *fn;
} k_hooks[] = {
    {"_ZN10FirebaseNS4InitEP7_JNIEnvP8_jobjectPKNS_14ConfigKeyValueEjPFvPKcEb", fb_init},
    {"_ZN10FirebaseNS14GetConfigValueEPKcPcj", fb_config_value},
    {"_ZN10FirebaseNS16GetDeepLinkValueEPcj", fb_no_string},
    {"_ZN10FirebaseNS17GetShorLinkResultEPc", fb_no_string},
    {"_ZN10FirebaseNS3EndEv", fb_nothing},
    {"_ZN10FirebaseNS6ResumeEv", fb_nothing},
    {"_ZN10FirebaseNS6UpdateEv", fb_nothing},
    {"_ZN10FirebaseNS17GenerateShortLinkEPKc", fb_nothing},
    {"_ZN10FirebaseNS16SetUserAttributeEPKcS1_", fb_nothing},
    {"_ZN10FirebaseNS11EventCustomEPKcPKNS_10EventParamEj", fb_nothing},
    {"_ZN10FirebaseNS11EventCustomEPKcS1_S1_", fb_nothing},
    {"_ZN10FirebaseNS11EventCustomEPKcS1_S1_S1_S1_", fb_nothing},
    {"_ZN10FirebaseNS11EventCustomEPKcS1_S1_S1_S1_S1_S1_", fb_nothing},
    {"_ZN10FirebaseNS12EventLevelUpEiPKc", fb_nothing},
    {"_ZN10FirebaseNS18EventTutorialBeginEv", fb_nothing},
    {"_ZN10FirebaseNS21EventTutorialCompleteEv", fb_nothing},
    {"_ZN10FirebaseNS22EventUnlockAchievementEPKc", fb_nothing},
    {"_ZN10FirebaseNS25EventSpendVirtualCurrencyEPKci", fb_nothing},
};

/* After the module is sealed as code, before any of it runs. */
void dtm_firebase_patch(void) {
  unsigned done = 0, total = sizeof k_hooks / sizeof k_hooks[0];
  for (unsigned i = 0; i < total; i++) {
    const uintptr_t fn = so_try_find_addr_rx(&g_mod_game, k_hooks[i].sym);
    if (!fn) {
      debugPrintf("[firebase] no %s in this engine\n", k_hooks[i].sym);
      continue;
    }
    if (dtm_hook(fn, k_hooks[i].fn) == 0)
      done++;
    else
      debugPrintf("[firebase] could not patch %s\n", k_hooks[i].sym);
  }
  debugPrintf("[firebase] %u of %u FirebaseNS functions replaced: Firebase stays off\n", done, total);
}
