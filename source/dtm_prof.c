/* dtm_prof.c -- where a long frame spends its time ([debug]
 * profile_long_frames in config.ini; off by default).
 *
 * The runtime's log says how long a long frame took and how much CPU each
 * thread used in it (dcr_boost.c), not what the engine was doing. This says
 * that: while a frame has been running for more than 80 ms, a thread of the
 * port's stops the main thread every few milliseconds, notes where it is
 * (its pc) and which engine functions are waiting on its stack (the return
 * addresses there that follow a call instruction of the engine's), and lets
 * it go. When the frame ends, the engine functions that were on the most
 * samples are written to the log, each with the share of the samples it was
 * on -- so the list reads from the frame's outermost function down to where
 * the time really goes.
 *
 * The sampling costs a little of the frame it measures, and nothing in a
 * normal frame. MIT.
 */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dtm.h"
#include "exc_handler.h"
#include "rt_applet.h"
#include "so_util.h"
#include "util.h"

#define SAMPLE_NS 4000000ll   /* between samples */
#define LONG_AFTER_MS 80      /* a frame is sampled from here on */
#define REPORT_FROM_MS 300    /* and reported when it took this long */
#define MAX_SAMPLES 400
#define CHAIN 7
#define STACK_SCAN 0x4000     /* bytes of stack looked at, above sp */
#define TOP 14
#define MAX_REPORTS 60

/* The kernel's ThreadContext; for an AArch32 thread r[0..14] are r0-r14
 * (as runtime/source/watchdog.c). */
typedef struct {
  uint64_t r[29];
  uint64_t fp, lr, sp, pc;
  uint32_t psr, _pad;
  uint8_t v[32][16];
  uint32_t fpcr, fpsr;
  uint64_t tpidr;
} KCtx;
_Static_assert(sizeof(KCtx) == 0x320, "kernel ThreadContext is 0x320 bytes");

/* svcGetThreadContext3: the 32-bit SVC ABI returns with r1-r3 zeroed, so
 * they are clobbers (as the runtime's watchdog found out). */
static Result get_ctx(KCtx *ctx, Handle h) {
  register uint32_t r0 __asm__("r0") = (uint32_t)(uintptr_t)ctx;
  register uint32_t r1 __asm__("r1") = h;
  __asm__ volatile("svc 0x33" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "lr", "memory");
  return r0;
}

/* ------------------------------------------------- the engine's functions */
typedef struct {
  uint32_t start, size, name; /* offsets into the module, and into its string table */
} Fn;
static Fn *g_fn;
static int g_nfn;
static uint16_t *g_hits; /* per function: samples it was on */
static uint32_t g_base, g_size;

static int cmp_fn(const void *a, const void *b) {
  const Fn *x = a, *y = b;
  return x->start < y->start ? -1 : x->start > y->start;
}

/* The function an address of the module is in, or -1. */
static int fn_of(uint32_t off) {
  int lo = 0, hi = g_nfn - 1;
  while (lo < hi) {
    const int mid = (lo + hi + 1) / 2;
    if (g_fn[mid].start <= off)
      lo = mid;
    else
      hi = mid - 1;
  }
  return g_nfn && off >= g_fn[lo].start && off < g_fn[lo].start + g_fn[lo].size ? lo : -1;
}

/* A word on the stack that is a return address into the engine: odd
 * (Thumb), inside the module, and just after a BL / BLX. */
static int is_return_address(uint32_t w) {
  if (!(w & 1))
    return 0;
  const uint32_t a = w & ~1u;
  if (a < g_base + 4 || a >= g_base + g_size)
    return 0;
  uint16_t h1, h2;
  memcpy(&h1, (const void *)(uintptr_t)(a - 4), 2);
  memcpy(&h2, (const void *)(uintptr_t)(a - 2), 2);
  return ((h1 & 0xf800) == 0xf000 && (h2 & 0xc000) == 0xc000) || (h2 & 0xff87) == 0x4780;
}

/* ------------------------------------------------------------ the samples */
typedef struct {
  uint32_t pc;
  uint32_t chain[CHAIN];
  uint8_t n;
} Sample;
static Sample g_samples[MAX_SAMPLES];
static int g_nsamples;

static Handle g_main;
static uintptr_t g_stack_top; /* nothing above this is looked at */
static volatile u64 g_frame_start;
static volatile uint32_t g_frame, g_last_ms;
static int g_on;

static void take_sample(void) {
  if (R_FAILED(svcSetThreadActivity(g_main, ThreadActivity_Paused)))
    return;
  KCtx ctx;
  if (R_SUCCEEDED(get_ctx(&ctx, g_main))) {
    Sample *s = &g_samples[g_nsamples++];
    s->pc = (uint32_t)ctx.pc;
    s->n = 0;
    uintptr_t sp = (uintptr_t)(uint32_t)ctx.r[13] & ~(uintptr_t)3;
    uintptr_t end = sp + STACK_SCAN;
    if (end > g_stack_top)
      end = g_stack_top;
    for (; sp + 4 <= end && s->n < CHAIN; sp += 4) {
      uint32_t w;
      memcpy(&w, (const void *)sp, 4);
      if (is_return_address(w))
        s->chain[s->n++] = w & ~1u;
    }
  }
  svcSetThreadActivity(g_main, ThreadActivity_Runnable);
}

static int cmp_hits(const void *a, const void *b) {
  const int x = *(const int *)a, y = *(const int *)b;
  return (int)g_hits[y] - (int)g_hits[x];
}

static void report(uint32_t frame, uint32_t ms) {
  static int touched[MAX_SAMPLES * (CHAIN + 1)];
  int ntouched = 0, outside = 0;
  char leaf_out[96] = "";
  for (int i = 0; i < g_nsamples; i++) {
    const Sample *s = &g_samples[i];
    int seen[CHAIN + 1], nseen = 0;
    const uint32_t pc = s->pc;
    if (pc >= g_base && pc < g_base + g_size) {
      const int f = fn_of(pc - g_base);
      if (f >= 0)
        seen[nseen++] = f;
    } else {
      if (!outside++)
        dcr_addr_name(pc, leaf_out, sizeof leaf_out);
    }
    for (int k = 0; k < s->n; k++) {
      const int f = fn_of(s->chain[k] - g_base);
      int dup = f < 0;
      for (int j = 0; j < nseen && !dup; j++)
        dup = seen[j] == f;
      if (!dup)
        seen[nseen++] = f;
    }
    for (int j = 0; j < nseen; j++)
      if (!g_hits[seen[j]]++)
        touched[ntouched++] = seen[j];
  }
  qsort(touched, (size_t)ntouched, sizeof touched[0], cmp_hits);
  debugPrintf("[prof] frame %u: %u ms, %d samples (%d%% of them outside the engine: libc, GL, the runtime; "
              "the first at %s)\n",
              (unsigned)frame, (unsigned)ms, g_nsamples, outside * 100 / g_nsamples, leaf_out[0] ? leaf_out : "-");
  for (int i = 0; i < ntouched && i < TOP; i++)
    debugPrintf("[prof]   %3d%%  %.110s\n", g_hits[touched[i]] * 100 / g_nsamples,
                g_mod_game.dynstrtab + g_fn[touched[i]].name);
  for (int i = 0; i < ntouched; i++)
    g_hits[touched[i]] = 0;
}

static void sampler(void *arg) {
  (void)arg;
  uint32_t frame = g_frame;
  int reports = 0;
  for (;;) {
    svcSleepThread(SAMPLE_NS);
    const uint32_t now_frame = g_frame;
    if (now_frame != frame) { /* the frame that was being sampled has ended */
      if (g_nsamples >= 10 && g_last_ms >= REPORT_FROM_MS && reports < MAX_REPORTS) {
        report(frame, g_last_ms);
        reports++;
      }
      g_nsamples = 0;
      frame = now_frame;
      continue;
    }
    if (!rt_focused() || dcr_applet_is_busy())
      continue; /* HOME, or a system screen: not the engine's time */
    if (armTicksToNs(armGetSystemTick() - g_frame_start) < (u64)LONG_AFTER_MS * 1000000ull)
      continue;
    if (g_nsamples < MAX_SAMPLES)
      take_sample();
  }
}

/* The frame loop, at each frame's end. */
void dtm_prof_frame(void) {
  if (!g_on)
    return;
  const u64 now = armGetSystemTick();
  g_last_ms = (uint32_t)(armTicksToNs(now - g_frame_start) / 1000000ull);
  g_frame_start = now;
  g_frame++;
}

/* On the main thread, from the function that runs the frame loop: every
 * frame's stack is below this call's. */
void dtm_prof_init(void) {
  if (!dcr_config()->profile)
    return;
  volatile int marker = 0;
  g_stack_top = (uintptr_t)&marker;
  (void)marker;
  g_main = envGetMainThreadHandle();
  g_base = (uint32_t)(uintptr_t)g_mod_game.load_virtbase;
  g_size = (uint32_t)g_mod_game.load_size;

  int n = 0;
  for (int i = 0; i < g_mod_game.num_syms; i++) {
    const Elf32_Sym *s = &g_mod_game.syms[i];
    n += s->st_shndx && s->st_size && ELF32_ST_TYPE(s->st_info) == STT_FUNC;
  }
  g_fn = malloc(sizeof *g_fn * (size_t)(n ? n : 1));
  g_hits = calloc((size_t)(n ? n : 1), sizeof *g_hits);
  if (!g_fn || !g_hits) {
    debugPrintf("[prof] no memory for the function list: no profile\n");
    return;
  }
  for (int i = 0; i < g_mod_game.num_syms; i++) {
    const Elf32_Sym *s = &g_mod_game.syms[i];
    if (s->st_shndx && s->st_size && ELF32_ST_TYPE(s->st_info) == STT_FUNC)
      g_fn[g_nfn++] = (Fn){s->st_value & ~1u, s->st_size, s->st_name};
  }
  qsort(g_fn, (size_t)g_nfn, sizeof *g_fn, cmp_fn);

  g_frame_start = armGetSystemTick();
  static Thread t;
  if (R_FAILED(threadCreate(&t, sampler, NULL, NULL, 0x8000, 0x2C, -2)) || R_FAILED(threadStart(&t))) {
    debugPrintf("[prof] no thread for the profile\n");
    return;
  }
  g_on = 1;
  debugPrintf("[prof] long frames are sampled (%d engine functions known); frames of %d ms and more are "
              "reported\n",
              g_nfn, REPORT_FROM_MS);
}
