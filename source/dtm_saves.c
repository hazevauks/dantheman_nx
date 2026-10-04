/* dtm_saves.c -- the game's saves, written to the SD card off the game's
 * thread.
 *
 * The engine saves often (at checkpoints, at a level's end, after a
 * purchase with gold), several files each time, and each the safe way
 * (Mortar::IFile_Direct): the new contents go to "<name>.jsontmp", and when
 * that is closed the old "<name>.json" is removed and the temporary file
 * renamed over it. On the SD card that is four slow operations a file --
 * creating it alone takes 20 to 80 ms (hardware logs: "[io] slow fopen") --
 * all inside one frame of the game, which then lasts 0.4 s and more.
 *
 * Here the temporary file is kept in memory, and the four operations are
 * done by a thread of the port's, in the same order: the temporary file
 * written whole, the old file removed, the new one renamed into place. So
 * what is on the card is always a whole old save or a whole new one, as the
 * engine intends; a power cut in the half second the thread needs loses only
 * that last save. Anything that then asks for one of those files (a read, a
 * stat, a remove, a rename) waits for the thread first, and the port waits
 * for it when the game leaves the screen or closes (dtm_saves_flush).
 *
 * The engine's fopen, remove, rename and stat reach these through the
 * runtime's import overlay (port_imports); everything else about the file
 * (fwrite, fseek, fclose...) is the runtime's, working on the FILE made here.
 * [performance] background_saves = false in config.ini turns it all off.
 * MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "bionic.h"
#include "dcr_config.h"
#include "dcr_dircache.h"
#include "dcr_path.h"
#include "dtm.h"
#include "imports.h"
#include "util.h"

/* the runtime's own (bionic_stdio.c, bionic_io.c) */
void *b_fopen(const char *path, const char *mode);
int b_remove(const char *path);
int b_rename(const char *from, const char *to);
int b_stat(const char *path, struct b_stat *out);

#define TMP_SUFFIX ".jsontmp" /* the engine's temporary name: "<name>.json" + "tmp" */
#define B_S_IFREG 0100000

enum { M_OPEN, M_CLOSED, M_QUEUED, M_WRITING };

typedef struct Mem {
  struct Mem *next;
  char *tmp;   /* the temporary file, on the card (sdmc:/...) */
  char *final; /* where it goes: set by the rename */
  uint8_t *data;
  size_t len, cap, pos;
  int state;
  unsigned seq; /* the order the renames came in */
} Mem;

static Mem *g_files;
static Mutex g_lock;
static CondVar g_work, g_done;
static Thread g_thread;
static int g_started;
static unsigned g_seq;
static struct {
  unsigned files, waits, failed;
  u64 bytes, longest_ms;
} g_stats;

static int ends_with(const char *s, const char *tail) {
  const size_t a = strlen(s), b = strlen(tail);
  return a >= b && !strcmp(s + a - b, tail);
}

static void mem_free(Mem *m) {
  free(m->tmp);
  free(m->final);
  free(m->data);
  free(m);
}

/* (locked) */
static void unlink_file(Mem *m) {
  for (Mem **p = &g_files; *p; p = &(*p)->next)
    if (*p == m) {
      *p = m->next;
      return;
    }
}

/* ------------------------------------------------- the file, in memory */
static int mem_read(void *c, char *buf, _READ_WRITE_BUFSIZE_TYPE n) {
  Mem *m = c;
  size_t left = m->pos < m->len ? m->len - m->pos : 0;
  if ((size_t)n > left)
    n = (_READ_WRITE_BUFSIZE_TYPE)left;
  memcpy(buf, m->data + m->pos, (size_t)n);
  m->pos += (size_t)n;
  return (int)n;
}

static int mem_write(void *c, const char *buf, _READ_WRITE_BUFSIZE_TYPE n) {
  Mem *m = c;
  const size_t end = m->pos + (size_t)n;
  if (end > m->cap) {
    size_t cap = m->cap ? m->cap : 16384;
    while (cap < end)
      cap *= 2;
    uint8_t *d = realloc(m->data, cap);
    if (!d)
      return -1;
    m->data = d;
    m->cap = cap;
  }
  if (m->pos > m->len) /* written past the end after a seek: the gap is zeros */
    memset(m->data + m->len, 0, m->pos - m->len);
  memcpy(m->data + m->pos, buf, (size_t)n);
  m->pos = end;
  if (end > m->len)
    m->len = end;
  return (int)n;
}

static fpos_t mem_seek(void *c, fpos_t off, int whence) {
  Mem *m = c;
  const long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long)m->pos : (long long)m->len;
  const long long np = base + (long long)off;
  if (np < 0)
    return -1;
  m->pos = (size_t)np;
  return (fpos_t)np;
}

/* fclose: the engine removes the old file and renames this one next. */
static int mem_close(void *c) {
  Mem *m = c;
  mutexLock(&g_lock);
  m->state = M_CLOSED;
  mutexUnlock(&g_lock);
  return 0;
}

/* ------------------------------------------------------------ the thread */
/* What the engine's own close would have done, on the card. */
static int write_out(const Mem *m) {
  FILE *f = fopen(m->tmp, "wb");
  if (!f)
    return -1;
  int ok = fwrite(m->data, 1, m->len, f) == m->len;
  ok = fclose(f) == 0 && ok;
  if (!ok) {
    remove(m->tmp);
    return -2;
  }
  remove(m->final); /* it may not exist yet */
  return rename(m->tmp, m->final) == 0 ? 0 : -3;
}

static void worker(void *arg) {
  (void)arg;
  for (;;) {
    mutexLock(&g_lock);
    Mem *m = NULL;
    for (;;) {
      for (Mem *k = g_files; k; k = k->next)
        if (k->state == M_QUEUED && (!m || (int)(k->seq - m->seq) < 0))
          m = k;
      if (m)
        break;
      condvarWait(&g_work, &g_lock);
    }
    m->state = M_WRITING;
    mutexUnlock(&g_lock);

    const u64 t0 = armGetSystemTick();
    int rc = write_out(m);
    if (rc != 0)
      rc = write_out(m); /* once more: the card may have been busy */
    dcr_dircache_forget(); /* the runtime's list of what exists: it changed */
    const u64 ms = armTicksToNs(armGetSystemTick() - t0) / 1000000ull;
    if (rc != 0)
      debugPrintf("[saves] COULD NOT WRITE %s (step %d): that save is lost, the one before it stays\n",
                  m->final, -rc);

    mutexLock(&g_lock);
    g_stats.files++;
    g_stats.failed += rc != 0;
    g_stats.bytes += m->len;
    if (ms > g_stats.longest_ms)
      g_stats.longest_ms = ms;
    unlink_file(m);
    condvarWakeAll(&g_done);
    mutexUnlock(&g_lock);
    mem_free(m);
  }
}

/* (locked) Is the thread still to write, or writing, this file? */
static int busy_with(const char *real) {
  for (Mem *m = g_files; m; m = m->next)
    if ((m->state == M_QUEUED || m->state == M_WRITING) && (!strcmp(m->tmp, real) || !strcmp(m->final, real)))
      return 1;
  return 0;
}

/* Until the card holds what the engine believes it does about this file. */
static void wait_for(const char *real) {
  mutexLock(&g_lock);
  if (busy_with(real)) {
    g_stats.waits++;
    while (busy_with(real))
      condvarWait(&g_done, &g_lock);
  }
  mutexUnlock(&g_lock);
}

/* Everything queued is on the card: when the game leaves the screen, and
 * when it closes. */
void dtm_saves_flush(void) {
  if (!g_started)
    return;
  mutexLock(&g_lock);
  for (;;) {
    int busy = 0;
    for (Mem *m = g_files; m && !busy; m = m->next)
      busy = m->state == M_QUEUED || m->state == M_WRITING;
    if (!busy)
      break;
    condvarWait(&g_done, &g_lock);
  }
  mutexUnlock(&g_lock);
}

/* For the log: what the thread has done since the last call. */
void dtm_saves_report(void) {
  if (!g_started)
    return;
  static unsigned last;
  mutexLock(&g_lock);
  const unsigned files = g_stats.files, waits = g_stats.waits, failed = g_stats.failed;
  const u64 bytes = g_stats.bytes, longest = g_stats.longest_ms;
  mutexUnlock(&g_lock);
  if (files == last)
    return;
  last = files;
  debugPrintf("[saves] %u file(s), %llu KB written in the background (the longest took %llu ms); "
              "%u wait(s), %u failed\n",
              files, (unsigned long long)(bytes >> 10), (unsigned long long)longest, waits, failed);
}

/* ----------------------------------------------- the engine's four calls */
/* (locked) A file the engine is writing, or has closed and not yet renamed. */
static Mem *find_unqueued(const char *real) {
  for (Mem *m = g_files; m; m = m->next)
    if ((m->state == M_OPEN || m->state == M_CLOSED) && !strcmp(m->tmp, real))
      return m;
  return NULL;
}

static void *saves_fopen(const char *path, const char *mode) {
  if (!g_started || !path || !mode)
    return b_fopen(path, mode);
  char buf[DCR_PATH_MAX];
  const char *real = dcr_translate_path(path, buf, sizeof buf);
  if (mode[0] != 'w' || !ends_with(real, TMP_SUFFIX)) {
    wait_for(real);
    return b_fopen(path, mode);
  }
  Mem *m = calloc(1, sizeof *m);
  if (m)
    m->tmp = strdup(real);
  FILE *f = m && m->tmp ? funopen(m, mem_read, mem_write, mem_seek, mem_close) : NULL;
  if (!f) { /* no memory: the engine's own way */
    if (m)
      mem_free(m);
    return b_fopen(path, mode);
  }
  mutexLock(&g_lock);
  Mem *old = find_unqueued(real); /* one never renamed: this replaces it */
  if (old && old->state == M_CLOSED)
    unlink_file(old);
  else
    old = NULL;
  m->next = g_files;
  g_files = m;
  mutexUnlock(&g_lock);
  if (old)
    mem_free(old);
  return f;
}

static int saves_rename(const char *from, const char *to) {
  if (!g_started || !from || !to)
    return b_rename(from, to);
  char b1[DCR_PATH_MAX], b2[DCR_PATH_MAX];
  const char *rfrom = dcr_translate_path(from, b1, sizeof b1);
  const char *rto = dcr_translate_path(to, b2, sizeof b2);
  mutexLock(&g_lock);
  Mem *m = find_unqueued(rfrom);
  if (m && m->state == M_CLOSED && (m->final = strdup(rto))) {
    m->seq = ++g_seq;
    m->state = M_QUEUED;
    condvarWakeOne(&g_work);
    mutexUnlock(&g_lock);
    return 0;
  }
  mutexUnlock(&g_lock);
  wait_for(rfrom);
  wait_for(rto);
  return b_rename(from, to);
}

static int saves_remove(const char *path) {
  if (!g_started || !path)
    return b_remove(path);
  char buf[DCR_PATH_MAX], tmp[DCR_PATH_MAX + 8];
  const char *real = dcr_translate_path(path, buf, sizeof buf);
  snprintf(tmp, sizeof tmp, "%stmp", real);
  mutexLock(&g_lock);
  /* the old file, about to be replaced by its temporary one: the thread
   * removes it, just before the rename */
  if (ends_with(tmp, TMP_SUFFIX) && find_unqueued(tmp)) {
    mutexUnlock(&g_lock);
    return 0;
  }
  /* the temporary file itself, given up */
  Mem *m = find_unqueued(real);
  if (m && m->state == M_CLOSED) {
    unlink_file(m);
    mutexUnlock(&g_lock);
    mem_free(m);
    return 0;
  }
  mutexUnlock(&g_lock);
  wait_for(real);
  return b_remove(path);
}

static int saves_stat(const char *path, struct b_stat *out) {
  if (!g_started || !path || !out)
    return b_stat(path, out);
  char buf[DCR_PATH_MAX];
  const char *real = dcr_translate_path(path, buf, sizeof buf);
  mutexLock(&g_lock);
  const Mem *m = find_unqueued(real);
  if (m) { /* the file in memory: it exists, this long */
    memset(out, 0, sizeof *out);
    out->st_mode = B_S_IFREG | 0644;
    out->st_size = (long long)m->len;
    mutexUnlock(&g_lock);
    return 0;
  }
  mutexUnlock(&g_lock);
  wait_for(real);
  return b_stat(path, out);
}

/* The runtime's import overlay: searched before its own table, for every
 * import of the engine's. */
const DynLibFunction port_imports[] = {
    {"fopen", (uintptr_t)saves_fopen},
    {"rename", (uintptr_t)saves_rename},
    {"remove", (uintptr_t)saves_remove},
    {"stat", (uintptr_t)saves_stat},
};
const int port_imports_count = (int)(sizeof port_imports / sizeof port_imports[0]);

/* Before the engine runs. */
void dtm_saves_init(void) {
  if (!dcr_config()->background_saves) {
    debugPrintf("[saves] written by the game's own thread (config.ini)\n");
    return;
  }
  mutexInit(&g_lock);
  condvarInit(&g_work);
  condvarInit(&g_done);
  if (R_FAILED(threadCreate(&g_thread, worker, NULL, NULL, 0x10000, 0x3B, -2)) ||
      R_FAILED(threadStart(&g_thread))) {
    debugPrintf("[saves] no thread for the saves: written by the game's own thread\n");
    return;
  }
  g_started = 1;
  debugPrintf("[saves] the game's saves are written to the card in the background\n");
}
