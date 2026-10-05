/* SPDX-License-Identifier: GPL-2.0-or-later
 * Common wording follows the PSP language files; Xbox-only button hints stay
 * here, so neither port links the other's hardware-dependent language code. */
#ifndef XBOX_UI_LANGUAGE_H
#define XBOX_UI_LANGUAGE_H
static int ui_language; /* 0=dashboard, 1=English, 2=Deutsch */
static int dashboard_german;
#define XSTR(id,en,de) XL_##id,
enum {
#include "language_strings.h"
    XL_COUNT
};
#undef XSTR
#define XSTR(id,en,de) {en,de},
static const char *const ui_strings[XL_COUNT][2]={
#include "language_strings.h"
};
#undef XSTR
static const char *xl(int id){return ui_strings[id][ui_language==2||(ui_language==0&&dashboard_german)];}
#define XL(id) xl(XL_##id)
static const char *ui_language_name(void){return ui_language==1?"English":ui_language==2?"Deutsch":"Dashboard (Auto)";}
#endif
