/* dtm_ads.c -- PERSONAL BUILD ONLY: the rewards the game gives for watching
 * an ad, without the ad.
 *
 * This file is not part of the public port. The game pays for itself with
 * ads; a Switch can show none, and the port's releases leave the rewards
 * unavailable. This is the owner's own build, off by default
 * ([personal] ad_rewards in config.ini).
 *
 * What the game does for a rewarded video (the wait before a level on the
 * story map, the continue, the checkpoint before a boss, the free gold):
 *   - the screen's video button is offered when GameAdvertising::AdPrepared
 *     says an ad is loaded for that reason;
 *   - pressing it, the handler first asks Mortar::Reachability whether there
 *     is an internet connection, then calls GameAdvertising::ShowAd with a
 *     delegate to run when the ad ends;
 *   - ShowAd pauses the game and, with no reliable time or no connection,
 *     ends at once through GameAdvertising::iShowCompleted(false, ...),
 *     which resumes the game and runs the delegate with "not watched".
 *
 * Three changes, all in the game's own layer (the engine is never told it is
 * online, so its account, analytics and store code stay as offline as they
 * were):
 *   1. AdPrepared answers yes.
 *   2. Reachability answers "connected" only to the video buttons' own
 *      functions (by the address the call returns to). The purchase code
 *      (GameStore::PurchaseItem) is not among them: buying stays impossible.
 *      Nor are the full-screen ads between levels, which reward nothing.
 *   3. ShowAd's no-connection ending passes true to iShowCompleted: "watched".
 *      One byte: movs r1, #0 -> movs r1, #1.
 *
 * Everything is found by name, and the one-byte patch only where the code is
 * the 1.2.1 code it was read from. MIT.
 */
#include <string.h>

#include "dcr_config.h"
#include "dtm.h"
#include "so_util.h"
#include "util.h"

/* ------------------------------------------------ 1. an ad is always ready */
static int ad_prepared(void *self, int reason) {
  (void)self, (void)reason;
  return 1;
}

/* ------------------------------------------ 2. connected, to the video buttons */
static const char *const k_video_callers[] = {
    "_ZN21GameScreenWeeklyEvent22AdButtonPressedHandlerEPN6Mortar9ComponentERb",
    "_ZN18GameScreenStoryMap14OnVideoPressedEPN6Mortar16ComponentTriggerE",
    "_ZN18GameScreenStoryMap17OnPlayLevelViewAdEPN6Mortar16ComponentTriggerE",
    "_ZN21GameScreenStoreDirect25ButtonVideoPressedHandlerEPN6Mortar9ComponentERb",
    "_ZN15GameScreenStore31SwipieButtonVideoPressedHandlerEPN6Mortar9ComponentERb",
    "_ZN14GameScreenPlay13PlayVideoItemEP14GameObjectItemb",
    "_ZN18GameScreenContinue25VideoButtonPressedHandlerEPN6Mortar9ComponentERb",
    "_ZN18GameScreenContinue16StateOpenedEnterEv",
    "_ZN23GameScreenArenaLevelEnd16StateOpenedEnterEv",
    "_ZN23GameScreenArenaLevelEnd24ItemButtonPressedHandlerEPN6Mortar9ComponentERb",
    "_ZN23GameScreenArenaCampaign30BuyGachaAdButtonPressedHandlerEPN6Mortar9ComponentERb",
};
#define NCALLERS (sizeof k_video_callers / sizeof k_video_callers[0])

static struct {
  uintptr_t start, end;
} g_ranges[NCALLERS];
static unsigned g_nranges;

/* Mortar::Reachability::ReachabilityForInternetConnection(): 0 is "none".
 * Entered by a jump from the function's first instruction, so the return
 * address is the caller's. */
static __attribute__((noinline)) int reachability(void) {
  const uintptr_t ra = (uintptr_t)__builtin_return_address(0) & ~(uintptr_t)1;
  for (unsigned i = 0; i < g_nranges; i++)
    if (ra >= g_ranges[i].start && ra < g_ranges[i].end)
      return 1;
  return 0;
}

/* A function's address and size, from the module's symbol table. */
static int find_range(const char *name, uintptr_t *start, uintptr_t *end) {
  for (int i = 0; i < g_mod_game.num_syms; i++) {
    const Elf32_Sym *s = &g_mod_game.syms[i];
    if (!s->st_shndx || !s->st_size || strcmp(g_mod_game.dynstrtab + s->st_name, name))
      continue;
    *start = ((uintptr_t)g_mod_game.load_virtbase + s->st_value) & ~(uintptr_t)1;
    *end = *start + s->st_size;
    return 1;
  }
  return 0;
}

/* ------------------------------------- 3. the ad that cannot be shown ends "watched" */
/* GameAdvertising::ShowAd, 1.2.1: at +0x18a
 *   movs r0, #0 ; movs r1, #0   (the null ad instance, and success = false)
 * before the call of iShowCompleted. */
#define SHOWAD_AT 0x18a
static int patch_show_ad(uintptr_t fn) {
  static const uint8_t k_was[4] = {0x00, 0x20, 0x00, 0x21}, k_watched = 0x01;
  uint8_t *at = (uint8_t *)(fn & ~(uintptr_t)1) + SHOWAD_AT;
  if (!(fn & 1) || memcmp(at, k_was, sizeof k_was))
    return -1;
  return so_patch_code(at + 2, &k_watched, 1);
}

/* After the module is sealed as code, before any of it runs. */
void dtm_ads_patch(void) {
  if (!dcr_config()->ad_rewards)
    return;
  const uintptr_t prepared =
      so_try_find_addr_rx(&g_mod_game, "_ZNK15GameAdvertising10AdPreparedEN20GameAdvertisingEnums8AdReasonE");
  const uintptr_t show = so_try_find_addr_rx(
      &g_mod_game,
      "_ZN15GameAdvertising6ShowAdEN20GameAdvertisingEnums8AdReasonEbPKcRKSsRKN6Mortar8DelegateIFvS5_bEEE");
  const uintptr_t reach =
      so_try_find_addr_rx(&g_mod_game, "_ZN6Mortar12Reachability33ReachabilityForInternetConnectionEv");
  for (unsigned i = 0; i < NCALLERS; i++)
    if (find_range(k_video_callers[i], &g_ranges[g_nranges].start, &g_ranges[g_nranges].end))
      g_nranges++;
    else
      debugPrintf("[ads] no %s in this engine\n", k_video_callers[i]);
  if (!prepared || !show || !reach || !g_nranges) {
    debugPrintf("[ads] the game's ad functions are not the ones this build knows: nothing changed\n");
    return;
  }
  /* the one-byte patch first: without it the other two would only offer
   * buttons that end in "not watched" */
  if (patch_show_ad(show) != 0) {
    debugPrintf("[ads] GameAdvertising::ShowAd is not the 1.2.1 code: nothing changed\n");
    return;
  }
  const int a = dtm_hook(prepared, ad_prepared), b = dtm_hook(reach, reachability);
  debugPrintf("[ads] PERSONAL BUILD: ad rewards without the ad (%s; %u of %u video buttons; "
              "purchases untouched)\n",
              a == 0 && b == 0 ? "on" : "partly: a patch failed", g_nranges, (unsigned)NCALLERS);
}
