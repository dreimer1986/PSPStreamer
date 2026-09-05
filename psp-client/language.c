#include <string.h>
#include "language.h"
#include "lang_en.h"
#include "lang_de.h"

typedef struct { const char *code; const char *const *text; } Language;
/* Register additional language files here.  Their array must follow TextId. */
static const Language languages[] = {{"en", lang_en}, {"de", lang_de}};
static int active_language;

const char *tr(TextId id) { return id >= 0 && id < TXT_COUNT ? languages[active_language].text[id] : ""; }
void language_set_code(const char *code) {
    int i;
    for (i = 0; i < (int)(sizeof(languages) / sizeof(languages[0])); i++)
        if (!strcmp(code, languages[i].code)) { active_language = i; return; }
    active_language = 0;
}
const char *language_code(void) { return languages[active_language].code; }
