/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSP_FTP_STARTUP_H
#define PSP_FTP_STARTUP_H
typedef struct { unsigned long long since; int stable; } FtpStartup;
/* A time delay alone does not establish that the VSH finished booting.
 * Require five uninterrupted seconds of ready shell state as well. */
static int ftp_startup_ready(FtpStartup *s,unsigned long long now,int ready) {
    if(!ready){s->stable=0;return 0;}
    if(!s->stable){s->stable=1;s->since=now;return 0;}
    return now>=s->since && now-s->since>=5000000ULL;
}
#endif
