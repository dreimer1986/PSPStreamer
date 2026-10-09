/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef struct {
    unsigned long long begin,changed;
    unsigned blocks;
    int initialized;
} StartupProgress;
static int startup_audio_stalled(StartupProgress *p,unsigned long long now,unsigned blocks) {
    if(!p->initialized){p->initialized=1;p->begin=p->changed=now;p->blocks=blocks;}
    if(blocks!=p->blocks){p->blocks=blocks;p->changed=now;}
    return now-p->begin>=20000000ULL && now-p->changed>=10000000ULL;
}
