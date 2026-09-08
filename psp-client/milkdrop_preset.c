/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded parser for a documented static subset, not the MilkDrop parser. */
#include "milkdrop_preset.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

MdFilePreset md_custom_preset;
static char *md_trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = 0;
    return text;
}
static int md_file_error(MdFileError *e, int code, int line, const char *key) {
    e->code = code; e->line = line;
    snprintf(e->key, sizeof(e->key), "%.39s", key);
    return code;
}
int md_load_preset(const char *path, MdFilePreset *out, MdFileError *error) {
    static const char *keys[] = {"zoom", "rot", "warp", "fWarpAnimSpeed", "fWarpScale",
                                 "fDecay", "wave_r", "wave_g", "wave_b"};
    static const float low[] = {.8f, -.2f, -4, 0, .1f, .8f, 0, 0, 0};
    static const float high[] = {1.2f, .2f, 4, 4, 8, 1, 1, 1, 1};
    MdFilePreset next = {md_presets[0], 1, .6f, .2f};
    float *values[] = {&next.warp.zoom, &next.warp.rotation, &next.warp.warp,
        &next.warp.warp_speed, &next.warp.warp_scale, &next.warp.decay,
        &next.red, &next.green, &next.blue};
    FILE *file = fopen(path, "rb");
    char line[256];
    int total = 0, number = 0, section = 0, result = MD_FILE_OK;
    unsigned int seen = 0;
    memset(error, 0, sizeof(*error));
    if (!file) return md_file_error(error, errno == ENOENT ? MD_FILE_MISSING : MD_FILE_IO, 0, "");
    for (;;) {
        int ch, used = 0, index;
        char *key, *value, *equal, *end;
        float parsed;
        number++;
        while ((ch = fgetc(file)) != EOF && ch != '\n') {
            if (++total > 16384 || used >= (int)sizeof(line)-1 || ch == 0) {
                result = md_file_error(error, MD_FILE_INVALID, number, "size/encoding");
                goto done;
            }
            line[used++] = (char)ch;
        }
        if (ch == '\n' && ++total > 16384) {
            result = md_file_error(error, MD_FILE_INVALID, number, "size");
            goto done;
        }
        if (ch == EOF && ferror(file)) { result = md_file_error(error, MD_FILE_IO, number, ""); goto done; }
        if (ch == EOF && !used) break;
        line[used] = 0; key = md_trim(line);
        /* ASCII fields, UTF-8 comments. Accept an optional UTF-8 BOM. */
        if (number == 1 && strlen(key) >= 3 && !memcmp(key, "\xef\xbb\xbf", 3)) key = md_trim(key+3);
        if (!*key || *key == ';' || *key == '#' || !strncmp(key, "//", 2)) continue;
        if (!strcmp(key, "[preset00]")) {
            if (section) { result = md_file_error(error, MD_FILE_INVALID, number, key); goto done; }
            section = 1; continue;
        }
        equal = strchr(key, '=');
        if (!section || !equal) { result = md_file_error(error, MD_FILE_INVALID, number, key); goto done; }
        *equal = 0; key = md_trim(key); value = md_trim(equal+1);
        for (index = 0; index < 9 && strcmp(key, keys[index]); index++) {}
        if (index == 9) { result = md_file_error(error, MD_FILE_UNSUPPORTED, number, key); goto done; }
        errno = 0; parsed = strtof(value, &end);
        if (end == value || *md_trim(end) || errno == ERANGE || !isfinite(parsed) ||
            parsed < low[index] || parsed > high[index] || (seen & (1U << index))) {
            result = md_file_error(error, MD_FILE_INVALID, number, key); goto done;
        }
        *values[index] = parsed; seen |= 1U << index;
    }
    if (!section || !seen) result = md_file_error(error, MD_FILE_INVALID, number, "empty");
done:
    if (fclose(file) && result == MD_FILE_OK)
        result = md_file_error(error, MD_FILE_IO, number, "");
    if (result == MD_FILE_OK) *out = next;
    return result;
}
