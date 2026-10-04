/* dtm_keystore.c -- com.halfbrick.mortar.KeyStore, kept in a file.
 *
 * On Android the class is a thin layer over SharedPreferences: the engine
 * keeps small strings there by name (which user's save is the current one,
 * among them), reading with GetValue(key) and writing with
 * SetValue(key, value), which answers whether the value was stored. Without
 * it nothing the engine puts there survives to the next launch.
 *
 * Here: <game folder>/data/keystore.txt, one "key<TAB>value" line per entry
 * (backslash, tab and line breaks escaped), read once and rewritten whole at
 * every change (through a .part file, so a power cut leaves the old one). A
 * key that was never set reads as "", as the engine's first launch on a
 * phone. MIT.
 */
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_setup.h"
#include "dtm.h"
#include "util.h"

#define STORE_FILE "data/keystore.txt"

typedef struct Entry {
  struct Entry *next;
  char *key, *value;
} Entry;

static Entry *g_entries;
static int g_loaded;
static Mutex g_lock;

/* Escaped length and text: \\ \t \n \r. */
static size_t escape(const char *s, char *out) {
  size_t n = 0;
  for (; *s; s++) {
    const char c = *s == '\\' ? '\\' : *s == '\t' ? 't' : *s == '\n' ? 'n' : *s == '\r' ? 'r' : 0;
    if (c) {
      if (out)
        out[n] = '\\', out[n + 1] = c;
      n += 2;
    } else {
      if (out)
        out[n] = *s;
      n++;
    }
  }
  return n;
}

static char *unescape(const char *s, size_t len) {
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  size_t n = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == '\\' && i + 1 < len) {
      const char c = s[++i];
      out[n++] = c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
    } else {
      out[n++] = s[i];
    }
  }
  out[n] = 0;
  return out;
}

static Entry *find(const char *key) {
  for (Entry *e = g_entries; e; e = e->next)
    if (!strcmp(e->key, key))
      return e;
  return NULL;
}

static void put(char *key, char *value) {
  Entry *e = calloc(1, sizeof *e);
  if (!e || !key || !value) {
    free(e), free(key), free(value);
    return;
  }
  e->key = key;
  e->value = value;
  e->next = g_entries;
  g_entries = e;
}

static void load(void) {
  if (g_loaded)
    return;
  g_loaded = 1;
  char path[512];
  rt_root_path(path, sizeof path, STORE_FILE);
  size_t len = 0;
  char *text = (char *)rt_read_whole(path, &len);
  int n = 0;
  for (char *p = text; p && *p;) {
    char *eol = strchr(p, '\n');
    const size_t line = eol ? (size_t)(eol - p) : strlen(p);
    const char *tab = memchr(p, '\t', line);
    if (tab) {
      put(unescape(p, (size_t)(tab - p)), unescape(tab + 1, line - (size_t)(tab + 1 - p)));
      n++;
    }
    if (!eol)
      break;
    p = eol + 1;
  }
  free(text);
  debugPrintf("[keystore] %s: %d value(s)\n", STORE_FILE, n);
}

static int save(void) {
  size_t cap = 1;
  for (Entry *e = g_entries; e; e = e->next)
    cap += escape(e->key, NULL) + 1 + escape(e->value, NULL) + 1;
  char *text = malloc(cap);
  if (!text)
    return 0;
  size_t n = 0;
  for (Entry *e = g_entries; e; e = e->next) {
    n += escape(e->key, text + n);
    text[n++] = '\t';
    n += escape(e->value, text + n);
    text[n++] = '\n';
  }
  char path[512];
  rt_root_path(path, sizeof path, STORE_FILE);
  const int ok = rt_write_atomic(path, text, n);
  free(text);
  if (!ok)
    debugPrintf("[keystore] could not write %s\n", STORE_FILE);
  return ok;
}

/* static String GetValue(String key) */
JNI_H_DECL(dtm_h_keystore_get) {
  const char *key = jni_utf(a[0].l);
  mutexLock(&g_lock);
  load();
  const Entry *e = find(key);
  JObj *out = jni_str(e ? e->value : "");
  mutexUnlock(&g_lock);
  return jv_l(out);
}

/* static boolean SetValue(String key, String value) */
JNI_H_DECL(dtm_h_keystore_set) {
  const char *key = jni_utf(a[0].l), *value = jni_utf(a[1].l);
  mutexLock(&g_lock);
  load();
  Entry *e = find(key);
  int ok = 1;
  if (e && !strcmp(e->value, value)) {
    /* unchanged: nothing to write */
  } else {
    if (e) {
      char *v = strdup(value);
      if (v) {
        free(e->value);
        e->value = v;
      } else {
        ok = 0;
      }
    } else {
      put(strdup(key), strdup(value));
    }
    ok = ok && save();
  }
  mutexUnlock(&g_lock);
  return jv_z(ok);
}
