/* SPDX-License-Identifier: GPL-2.0-or-later
 * Late USB consumer. Initial ME setup is owned by the small loader.
 */
#include "pops_audio_link.h"
#ifdef POPS_AUDIO_HOST_TEST
#define POPS_AUDIO_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define POPS_AUDIO_BARRIER() __asm__ volatile("sync" ::: "memory")
#endif
static PopsAudioService pops_audio_service;
static PopsAudioShared *pops_audio_shared;
static unsigned pops_audio_published;
static int pops_audio_mode;
static int pops_audio_refresh(unsigned op) {
    PopsAudioLink link={.abi=POPS_AUDIO_ABI};
    int rc=pops_audio_service(op,&link);
    if(rc<0 || link.abi!=POPS_AUDIO_ABI)return -25;
    pops_audio_shared=link.shared;pops_audio_published=link.published;
    return link.status;
}
static int pops_audio_install(void) {
    pops_audio_service=(void *)sctrlHENFindFunction("PSPConsolizer","ConsolizerAudio",0xB05501FB);
    if(!pops_audio_service)return -26;
    int rc=pops_audio_refresh(1);
    if(rc<0) {if(rc!=-25)pops_audio_refresh(2);pops_audio_service=NULL;return rc;}
    pops_audio_mode=1;
    audio_mirror_log("POPS early producer acquired by USB worker",pops_audio_published);
    return 0;
}
static void pops_audio_uninstall(void) {
    if(pops_audio_shared)pops_audio_shared->enabled=0;
    if(pops_audio_service)pops_audio_refresh(2);
    pops_audio_service=NULL;pops_audio_shared=NULL;pops_audio_mode=0;
}
static void pops_audio_collect(AudioMirror *m) {
    if(!pops_audio_service || pops_audio_refresh(0)<0){m->fault=3;return;}
    if(!pops_audio_shared)return;
    PopsAudioShared *s=pops_audio_shared;
    s->enabled=m->enabled && audio_mirror_allowed && !app_owner;
    POPS_AUDIO_BARRIER();
    unsigned wr=s->wr,rd=s->rd;
    if(!s->enabled){s->rd=wr;return;}
    unsigned count=wr-rd;
    if(count>POPS_AUDIO_RING){s->enabled=0;m->fault=3;return;}
    unsigned free=MIRROR_RING-(m->wr-m->rd);
    if(count>free)count=free;
    for(unsigned i=0;i<count;i++)m->words[(m->wr+i)&(MIRROR_RING-1)]=s->pcm[(rd+i)&(POPS_AUDIO_RING-1)];
    POPS_AUDIO_BARRIER();
    s->rd=rd+count;m->wr+=count;m->blocks+=count/64;m->dropped=s->dropped;
}
