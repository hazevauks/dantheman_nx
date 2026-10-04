/* dtm_locale.c -- the language the game runs in: the console's, or the one
 * config.ini names.
 *
 * On Android the game asks Java: Locale.getDefault().getLanguage() goes to
 * NativeGameLib.SystemInit ("pt"), and HBSupport answers GetCountry ("BR")
 * and GetDeviceLanguage / GetDeviceLocale ("pt-BR"). Its string tables:
 * English, Spanish (Spain and Latin America), German, French, Italian,
 * Japanese, Portuguese, Russian, Turkish, Chinese (both scripts); anything
 * else falls back to English in the game itself.
 *
 * Here the console's language (set:sys's language code: "en-US", "pt-BR",
 * "ja", "zh-Hans"...) is turned into those three, unless [game] language in
 * config.ini is something other than "auto" (the same kind of code: "en",
 * "pt-BR", "es-419"). Worked out once, at the first question. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dtm.h"
#include "util.h"

static char g_lang[4] = "en", g_country[4] = "US", g_tag[12] = "en-US";
static int g_done;

/* The country a language alone stands for (the console's plain codes). */
static const char *default_country(const char *lang) {
  static const char *const k[][2] = {{"en", "US"}, {"ja", "JP"}, {"fr", "FR"}, {"de", "DE"},
                                     {"it", "IT"}, {"es", "ES"}, {"ko", "KR"}, {"nl", "NL"},
                                     {"pt", "PT"}, {"ru", "RU"}, {"tr", "TR"}, {"zh", "CN"}};
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!strcmp(lang, k[i][0]))
      return k[i][1];
  return "US";
}

/* "pt-BR", "ja", "zh-Hans", "es-419": 1 when it is a code. */
static int parse(const char *code) {
  char lang[4] = "", rest[8] = "";
  const char *dash = strchr(code, '-');
  const size_t n = dash ? (size_t)(dash - code) : strlen(code);
  if (n < 2 || n > 3)
    return 0;
  memcpy(lang, code, n);
  if (dash)
    snprintf(rest, sizeof rest, "%s", dash + 1);
  for (char *p = lang; *p; p++)
    if (*p >= 'A' && *p <= 'Z')
      *p += 'a' - 'A';
  /* the scripts of Chinese, as Android's regions */
  if (!strcmp(rest, "Hans"))
    strcpy(rest, "CN");
  else if (!strcmp(rest, "Hant"))
    strcpy(rest, "TW");
  if (strlen(rest) < 2 || strlen(rest) > 3)
    snprintf(rest, sizeof rest, "%s", default_country(lang));
  snprintf(g_lang, sizeof g_lang, "%s", lang);
  snprintf(g_country, sizeof g_country, "%s", rest);
  snprintf(g_tag, sizeof g_tag, "%s-%s", g_lang, g_country);
  return 1;
}

static void work_out(void) {
  if (g_done)
    return;
  g_done = 1;
  const char *want = dcr_config()->language;
  if (want[0] && strcmp(want, "auto")) {
    if (parse(want)) {
      debugPrintf("[locale] %s (config.ini): language %s, country %s\n", want, g_lang, g_country);
      return;
    }
    debugPrintf("[locale] config.ini's language \"%s\" is not a language code: the console's\n", want);
  }
  u64 code = 0;
  Result rc = setInitialize();
  if (R_SUCCEEDED(rc)) {
    rc = setGetSystemLanguage(&code);
    setExit();
  }
  char text[9] = "";
  memcpy(text, &code, 8);
  if (R_SUCCEEDED(rc) && parse(text))
    debugPrintf("[locale] the console's language %s: language %s, country %s\n", text, g_lang, g_country);
  else
    debugPrintf("[locale] the console's language could not be read (0x%x): English\n", (unsigned)rc);
}

const char *dtm_language(void) {
  work_out();
  return g_lang;
}

const char *dtm_country(void) {
  work_out();
  return g_country;
}

const char *dtm_locale_tag(void) {
  work_out();
  return g_tag;
}
