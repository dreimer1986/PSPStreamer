/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#include "../psp-controller/pops_audio_link.h"
#define MIRROR_RING 4096U
typedef struct {unsigned enabled,wr,rd,words[MIRROR_RING],blocks,dropped,fault;} AudioMirror;
static int audio_mirror_allowed=1,app_owner;
static PopsAudioShared shared;
static int held,service_status,published;
static int service(unsigned op,PopsAudioLink *link) {
    if(op==1){if(held)return -1;held=1;}
    if(op==2)held=0;
    link->shared=&shared;link->published=published;link->status=service_status;return 0;
}
static void *sctrlHENFindFunction(const char *module,const char *library,unsigned nid) {
    assert(!strcmp(module,"PSPConsolizer") && !strcmp(library,"ConsolizerAudio") && nid==0xB05501FB);
    return (void *)service;
}
static void audio_mirror_log(const char *message,int rc){(void)message;(void)rc;}
/* Host compiler has no MIPS sync instruction; preserve a compiler barrier. */
#define POPS_AUDIO_HOST_TEST
#include "../psp-controller/pops_audio.h"
int main(void) {
    AudioMirror m={0};
    assert(pops_audio_install()==0 && held && pops_audio_mode && !pops_audio_published);
    pops_audio_collect(&m);assert(!shared.enabled);
    published=1;m.enabled=1;shared.wr=1;shared.pcm[0]=0x12345678;
    pops_audio_collect(&m);assert(shared.enabled && pops_audio_published && m.wr==1 && m.words[0]==0x12345678);
    app_owner=1;shared.wr=2;pops_audio_collect(&m);assert(!shared.enabled && shared.rd==2 && m.wr==1);
    pops_audio_uninstall();assert(!held && !pops_audio_mode && !pops_audio_service);
    service_status=-22;assert(pops_audio_install()==-22 && !held);
    held=1;assert(pops_audio_install()==-25 && held); /* do not release another owner */
    return 0;
}
