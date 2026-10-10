/* SPDX-License-Identifier: GPL-2.0-or-later
 * Narrow observer for the captured 6.61 game-plugin image helper. Changes only
 * two background-loader call sites, never the original arguments, result or surfaces.
 * User execution never jumps to our kernel code. All I/O stays in the worker.
 */
#include "xmb_texture_shared.h"
#include "bridge/xmb_texture_payload.inc"
static int xmb_texture_enabled,xmb_texture_installed,xmb_texture_pinned;
static SceUID xmb_texture_memory=-1,xmb_texture_uid=-1;
static XmbTextureShared *xmb_texture_shared;
static uint32_t xmb_texture_saved[2],xmb_texture_jump;
static uintptr_t xmb_texture_entry;
static unsigned xmb_texture_written;
static unsigned long long xmb_texture_after,xmb_texture_deadline;

static int xmb_texture_stop(void) {
    if(xmb_texture_shared)xmb_texture_shared->enabled=0;
    if(xmb_texture_installed){
        int intr=sceKernelCpuSuspendIntr();
        SceModule *m=sceKernelFindModuleByUID(xmb_texture_uid);
        if(m && m->text_addr+0x1bda0==xmb_texture_entry){
            volatile uint32_t *a=(void *)(m->text_addr+0xb51c),*b=(void *)(m->text_addr+0xb484);
            if(*a==xmb_texture_jump)*a=xmb_texture_saved[0];else xmb_texture_pinned=1;
            if(*b==xmb_texture_jump)*b=xmb_texture_saved[1];else xmb_texture_pinned=1;
            sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
        }
        xmb_texture_installed=0;
        sceKernelCpuResumeIntr(intr);
    }
    /* Another thread may have fetched the jump before restoration. Keep the
     * tiny, self-contained user allocation until VSH teardown, like POPS's
     * published callback. It contains no pointers into this kernel plugin. */
    if(xmb_texture_shared && xmb_texture_shared->users)return -1;
    return xmb_texture_pinned?-1:0;
}
static int xmb_texture_install(SceModule *m) {
    if(sceKernelDevkitVersion()!=0x06060110 || !m || m->text_size!=190664 ||
       m->data_size!=84 || m->bss_size!=2492 || m->nsegment!=2 ||
       !xmb_probe_range(m->text_addr,m->text_size,xmb_probe_ram_end(sceKernelGetModel())))return -1;
    uint32_t *p=(void *)(m->text_addr+0x1bda0);
    /* Captured prologue plus call sites at the end of both PNG branches.
     * Replace only atomic JAL words, not a live function prologue; leave both
     * original delay slots and the entire original loader intact. */
    if(p[0]!=0x27bdffe0 || p[1]!=0xafb50014 || p[2]!=0x00e0a821 || p[3]!=0xafb40010 ||
       *(uint32_t *)(m->text_addr+0xb51c)!=(0x0c000000U|((m->text_addr+0x1bda0)>>2)) ||
       *(uint32_t *)(m->text_addr+0xb484)!=(0x0c000000U|((m->text_addr+0x1bda0)>>2)))return -2;
    SceUID module_uid=m->modid;
    uintptr_t module_text=m->text_addr;
    unsigned context_offset=(64+sizeof(xmb_texture_code)+63)&~63U;
    xmb_texture_memory=sceKernelAllocPartitionMemory(2,"Consolizer XMB texture observer",PSP_SMEM_High,
        context_offset+sizeof(XmbTextureShared),NULL);
    if(xmb_texture_memory<0)return xmb_texture_memory;
    unsigned char *mem=sceKernelGetBlockHeadAddr(xmb_texture_memory);
    if(!mem || (uintptr_t)mem<0x08800000U ||
       !xmb_probe_range((uintptr_t)mem,context_offset+sizeof(XmbTextureShared),xmb_probe_ram_end(sceKernelGetModel()))){
        sceKernelFreePartitionMemory(xmb_texture_memory);xmb_texture_memory=-1;return -3;
    }
    memset(mem,0,context_offset+sizeof(XmbTextureShared));
    XmbTextureShared *ctx=(void *)(mem+context_offset);
    uint32_t *stub=(void *)mem;
    uintptr_t address=(uintptr_t)ctx;
    stub[0]=0x3c0b0000U|(address>>16); /* eighth EABI argument: t3 */
    stub[1]=0x356b0000U|(address&65535);
    stub[2]=0x08000000U|(((uintptr_t)(mem+64)>>2)&0x03ffffffU);stub[3]=0;
    memcpy(mem+64,xmb_texture_code,sizeof(xmb_texture_code));
    ctx->original=(uintptr_t)p;ctx->ram_end=xmb_probe_ram_end(sceKernelGetModel());ctx->enabled=1;
    volatile uint32_t *a=(void *)(module_text+0xb51c),*b=(void *)(module_text+0xb484);
    uint32_t original_call=0x0c000000U|(((uintptr_t)p>>2)&0x03ffffffU);
    xmb_texture_jump=0x0c000000U|(((uintptr_t)mem>>2)&0x03ffffffU);
    int intr=sceKernelCpuSuspendIntr();
    /* Allocation can yield. Do not publish into an unloaded/replaced module. */
    m=sceKernelFindModuleByUID(module_uid);
    if(!m || m->text_addr!=module_text || m->text_size!=190664 ||
       *a!=original_call || *b!=original_call ||
       a[1]!=0x00005021 || b[1]!=0x00005021){
        sceKernelCpuResumeIntr(intr);
        sceKernelFreePartitionMemory(xmb_texture_memory);xmb_texture_memory=-1;return -4;
    }
    xmb_texture_saved[0]=*a;xmb_texture_saved[1]=*b;
    xmb_texture_entry=(uintptr_t)p;xmb_texture_uid=module_uid;
    xmb_texture_shared=ctx;
    *a=xmb_texture_jump;*b=xmb_texture_jump;
    sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
    xmb_texture_installed=1;
    sceKernelCpuResumeIntr(intr);
    return 0;
}
static void xmb_texture_update(unsigned long long now,int allowed) {
    if(!xmb_texture_enabled || !controller_report || sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_VSH)return;
    if(!xmb_texture_after){xmb_texture_after=now+8000000ULL;return;}
    if(!allowed){xmb_texture_stop();xmb_texture_enabled=0;return;}
    if(now<xmb_texture_after)return;
    if(!xmb_texture_shared){
        xmb_texture_after=now+500000;
        SceModule *m=sceKernelFindModuleByName("game_plugin_module");if(!m)return;
        int rc=xmb_texture_install(m);controller_log("XMB texture observer install",rc);
        if(rc<0){xmb_texture_enabled=0;return;}
        xmb_texture_deadline=now+90000000ULL;
        sceIoMkdir(XMB_PROBE_DIR,0777);
        int fd=sceIoOpen(XMB_PROBE_DIR "/textures.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
        if(fd<0){xmb_texture_stop();xmb_texture_enabled=0;return;}
        const char header[]="XMB texture observer v1; original image loader preserved; no animation yet\n";
        int result=xmb_probe_write(fd,header,sizeof(header)-1);sceIoClose(fd);
        if(result<0){xmb_texture_stop();xmb_texture_enabled=0;return;}
    }
    XmbTextureShared *ctx=xmb_texture_shared;
    if(xmb_texture_written<ctx->produced && xmb_texture_written<XMB_TEXTURE_RECORDS){
        __sync_synchronize();
        const XmbTextureRecord *r=&ctx->records[xmb_texture_written];
        char line[640];int n=snprintf(line,sizeof(line),"record=%u key=%.31s result=%08X input_size=%u input_fnv=%08X surface=%08X flags=%u words=",
            xmb_texture_written,r->key,(unsigned)r->result,(unsigned)r->input_size,
            (unsigned)r->input_hash,(unsigned)r->surface,(unsigned)r->flags);
        for(unsigned i=0;i<26;i++)n+=snprintf(line+n,sizeof(line)-n,"%08X%s",(unsigned)r->words[i],i==25?"\n":",");
        int fd=sceIoOpen(XMB_PROBE_DIR "/textures.txt",PSP_O_WRONLY|PSP_O_APPEND,0666);
        int rc=fd<0?-1:xmb_probe_write(fd,line,n);if(fd>=0)sceIoClose(fd);
        if(rc<0){xmb_texture_stop();xmb_texture_enabled=0;return;}
        xmb_texture_written++;
    }
    if(now>=xmb_texture_deadline || xmb_texture_written==XMB_TEXTURE_RECORDS){
        xmb_texture_stop();xmb_texture_enabled=0;controller_log("XMB texture observer finished",xmb_texture_written);
    }
}
