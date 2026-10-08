/* SPDX-License-Identifier: GPL-2.0-or-later
 * Intercept main-CPU setup BEFORE the ME starts. Never patch a running ME.
 * Published producer code/context live in user partition 2 until POPS exits:
 * they contain no references to our kernel module and survive its unload.
 */
#include "pops_audio_shared.h"
static PopsAudioShared *pops_audio_shared;
static SceUID pops_audio_memory=-1;
static unsigned char *pops_audio_payload;
static volatile uint32_t *pops_audio_setup;
static uint32_t pops_audio_saved[2],pops_audio_jump;
static uint32_t pops_audio_trampoline[4] __attribute__((aligned(64)));
static volatile unsigned pops_audio_users,pops_audio_published;
static unsigned pops_audio_base;
static int pops_audio_pinned;

static int pops_audio_setup_hook(unsigned callback,unsigned stack) {
    pops_audio_users++;
    unsigned chosen=callback;
    /* Accept only the verified loaded POPS module's text, never an arbitrary
     * caller-supplied address. First successful publication is immutable. */
    if(!pops_audio_published && callback==pops_audio_base && pops_audio_shared) {
        pops_audio_shared->original=callback;
        pops_audio_code((uint32_t *)pops_audio_payload,callback,(uintptr_t)pops_audio_shared);
        sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
        __asm__ volatile("sync" ::: "memory");
        pops_audio_published=1;
        chosen=(uintptr_t)pops_audio_payload;
    } else if(pops_audio_published && callback==pops_audio_shared->original) {
        chosen=(uintptr_t)pops_audio_payload;
    }
    int (*original)(unsigned,unsigned)=(void *)pops_audio_trampoline;
    int rc=original(chosen,stack);
    pops_audio_users--;
    return rc;
}
static int pops_audio_install(void) {
    SceModule *m=sceKernelFindModuleByName("scePops_Manager");
    SceModule *p=sceKernelFindModuleByName("pops");
    if(sceKernelDevkitVersion()!=0x06060110 || !m || !p || m->text_size!=18832 ||
       m->nsegment!=2 || m->data_size!=68 || m->bss_size!=920 || m->segmentsize[1]<988 ||
       !audio_probe_text_valid(m->text_addr,m->text_size,m->segmentaddr,m->segmentsize,m->nsegment) ||
       m->segmentaddr[1]!=m->text_addr+18832 ||
       p->text_size!=1048036 || p->text_addr<0x08800000U || p->text_addr>0x09e00000U)return -20;
    const uint32_t *code=(const uint32_t *)m->text_addr;
    unsigned state=((code[0x2fc8/4]&65535U)<<16)+(int16_t)code[0x2fcc/4];
    if(state!=m->segmentaddr[1]+0x2cc ||
       (((code[0x34d0/4]&65535U)<<16)+(int16_t)code[0x34f0/4])!=state ||
       (((code[0x34bc/4]&65535U)<<16)+(int16_t)code[0x34c0/4])!=m->text_addr+0x2f28 ||
       pops_audio_hash(code,m->text_addr,0x2f88,0x31ec)!=0x03ea3a65U ||
       pops_audio_hash(code,m->text_addr,0x3490,0x3514)!=0x73444fc4U ||
       code[0x347c/4]!=0x3404ac44U ||
       mirror_jtarget(code[0x3478/4],m->text_addr+0x3478)!=m->text_addr+0x3ce4)return -21;
    /* A nonzero callback means bootstrap has already been configured. Do not
     * force a reset or try to invalidate the other CPU's live instruction cache. */
    if(*(volatile uint32_t *)(state|0x20000000U)) {
        pops_audio_boot_log("POPS ME already configured; no live patch or forced reset",-22);return -22;
    }
    pops_audio_memory=sceKernelAllocPartitionMemory(2,"Consolizer ME PCM",PSP_SMEM_High,
        192+sizeof(PopsAudioShared),NULL);
    if(pops_audio_memory<0)return pops_audio_memory;
    pops_audio_payload=sceKernelGetBlockHeadAddr(pops_audio_memory);
    if(!pops_audio_payload || (uintptr_t)pops_audio_payload<0x08800000U ||
       (uintptr_t)pops_audio_payload+192+sizeof(PopsAudioShared)>0x0a000000U)goto fail;
    memset(pops_audio_payload,0,192+sizeof(PopsAudioShared));
    sceKernelDcacheWritebackInvalidateAll();
    pops_audio_shared=(void *)(((uintptr_t)pops_audio_payload+192)|0xa0000000U);
    pops_audio_base=p->text_addr;
    pops_audio_setup=(void *)(m->text_addr+0x3490);
    pops_audio_saved[0]=pops_audio_setup[0];pops_audio_saved[1]=pops_audio_setup[1];
    pops_audio_trampoline[0]=pops_audio_saved[0];pops_audio_trampoline[1]=pops_audio_saved[1];
    pops_audio_trampoline[2]=0x08000000U|(((uintptr_t)(pops_audio_setup+2)>>2)&0x03ffffffU);
    pops_audio_trampoline[3]=0;
    pops_audio_jump=0x08000000U|(((uintptr_t)pops_audio_setup_hook>>2)&0x03ffffffU);
    int intr=sceKernelCpuSuspendIntr();
    /* Recheck the publication slot while the main CPU cannot start the ME. */
    if(*(volatile uint32_t *)(state|0x20000000U)) {
        sceKernelCpuResumeIntr(intr);pops_audio_setup=NULL;goto fail;
    }
    pops_audio_setup[0]=pops_audio_jump;pops_audio_setup[1]=0;
    sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
    sceKernelCpuResumeIntr(intr);
    pops_audio_boot_log("POPS ME setup hook armed (waiting for original bootstrap)",0);
    return 0;
fail:
    sceKernelFreePartitionMemory(pops_audio_memory);pops_audio_memory=-1;
    pops_audio_payload=NULL;pops_audio_shared=NULL;return -23;
}
static void pops_audio_uninstall(void) {
    if(pops_audio_shared)pops_audio_shared->enabled=0;
    if(pops_audio_setup) {
        int intr=sceKernelCpuSuspendIntr();
        if(pops_audio_setup[0]==pops_audio_jump && pops_audio_setup[1]==0) {
            pops_audio_setup[0]=pops_audio_saved[0];pops_audio_setup[1]=pops_audio_saved[1];
            sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
        } else pops_audio_pinned=1;
        sceKernelCpuResumeIntr(intr);
        while(pops_audio_users)sceKernelDelayThread(1000);
        if(!pops_audio_pinned)pops_audio_setup=NULL;
    }
    /* A published ME callback may be cached or called again after resume.
     * Keep its self-contained user allocation until process teardown. */
    if(pops_audio_memory>=0 && !pops_audio_published && !pops_audio_pinned) {
        sceKernelFreePartitionMemory(pops_audio_memory);pops_audio_memory=-1;pops_audio_shared=NULL;
    }
}
