/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XBOX_HTTP_STATUS_H
#define XBOX_HTTP_STATUS_H
/* Avoid scanf assignment suppression: the pinned nxdk PDCLib consumes and
 * writes a destination even for %*s, corrupting the following argument. */
static int xbox_http_status(const char *s, unsigned length) {
    if (length < 13 || s[0]!='H' || s[1]!='T' || s[2]!='T' || s[3]!='P' ||
        s[4]!='/' || s[5]!='1' || s[6]!='.' || (s[7]!='0' && s[7]!='1') ||
        s[8]!=' ' || s[9]<'1' || s[9]>'5' || s[10]<'0' || s[10]>'9' ||
        s[11]<'0' || s[11]>'9' || (s[12]!=' ' && s[12]!='\r')) return 0;
    return (s[9]-'0')*100+(s[10]-'0')*10+s[11]-'0';
}
#endif
