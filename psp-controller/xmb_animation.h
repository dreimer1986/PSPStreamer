/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional 6.61 background animation. Kernel worker performs bounded asset I/O;
 * all patched PAF/game-plugin calls go to self-contained USER code.
 */
#include "xmb_animation_shared.h"
#include "bridge/xmb_animation_payload.inc"
#define XMB_ANIMATION_PATH "ms0:/SEPLUGINS/PSPConsolizer/xmb-background.xba"
static int xmb_animation_enabled=1,xmb_animation_failed,xmb_animation_published;
static SceUID xmb_animation_memory=-1,xmb_animation_fd=-1,xmb_animation_paf_uid=-1,xmb_animation_game_uid=-1;
static uintptr_t xmb_animation_paf,xmb_animation_game;
static unsigned char *xmb_animation_mem;
static XmbAnimationShared *xmb_animation;
static unsigned xmb_animation_loaded,xmb_animation_bytes,xmb_animation_hash=2166136261U;
static unsigned xmb_animation_load_stub,xmb_animation_bind_stub,xmb_animation_dtor_stub[2];
static unsigned long long xmb_animation_after,xmb_animation_poll,xmb_animation_epoch,xmb_animation_log_after;
static unsigned xmb_animation_last_load;
static unsigned xmb_animation_jal(uintptr_t p){return 0x0c000000U|((p>>2)&0x03ffffffU);}
static void xmb_animation_stub(unsigned char *mem,unsigned offset,unsigned reg,uintptr_t ctx,unsigned code,int which) {
    uint32_t *s=(void *)(mem+offset);
    s[0]=0x3c000000U|(reg<<16)|(ctx>>16);
    s[1]=0x34000000U|(reg<<21)|(reg<<16)|(ctx&65535);
    s[2]=0x08000000U|(((uintptr_t)(mem+128+code)>>2)&0x03ffffffU);
    s[3]=which<0?0:0x24060000U|(unsigned)which;
}
static int xmb_animation_game_valid(SceModule *m) {
    if(!m || m->text_size!=190664 || m->data_size!=84 || m->bss_size!=2492 ||
       !xmb_probe_range(m->text_addr,m->text_size,xmb_probe_ram_end(sceKernelGetModel())))return 0;
    const uint32_t *p=(void *)(m->text_addr+0x1bda0),*call=(void *)(m->text_addr+0xb51c);
    return p[0]==0x27bdffe0 && p[1]==0xafb50014 && p[2]==0x00e0a821 && p[3]==0xafb40010 &&
        call[0]==xmb_animation_jal(m->text_addr+0x1bda0) && call[1]==0x00005021;
}
static int xmb_animation_paf_valid(SceModule *p) {
    if(!p || p->text_size!=1623320 ||
       !xmb_probe_range(p->text_addr,p->text_size,xmb_probe_ram_end(sceKernelGetModel())))return 0;
    const uint32_t *call=(void *)(p->text_addr+0x132a5c),*vt=(void *)(p->text_addr+0x18c4d0);
    const uint32_t *bind=(void *)(p->text_addr+0x134194);
    return call[0]==xmb_animation_jal(p->text_addr+0x134194) && call[1]==0x02002021 &&
        bind[0]==0x27bdffe0 && bind[1]==0xafb20008 && bind[2]==0xafb10004 &&
        vt[0]==p->text_addr+0x13267c && vt[1]==p->text_addr+0x1327c4;
}
static void xmb_animation_stop(void) {
    if(xmb_animation)xmb_animation->active=0;
    __sync_synchronize();
    if(xmb_animation_fd>=0){sceIoClose(xmb_animation_fd);xmb_animation_fd=-1;}
    if(xmb_animation_published){
        int intr=sceKernelCpuSuspendIntr();
        SceModule *g=sceKernelFindModuleByUID(xmb_animation_game_uid);
        SceModule *p=sceKernelFindModuleByUID(xmb_animation_paf_uid);
        if(g && g->text_addr==xmb_animation_game){
            uint32_t *call=(void *)(g->text_addr+0xb51c);
            if(*call==xmb_animation_jal(xmb_animation_load_stub))*call=xmb_animation_jal(g->text_addr+0x1bda0);
        }
        if(p && p->text_addr==xmb_animation_paf){
            uint32_t *call=(void *)(p->text_addr+0x132a5c),*vt=(void *)(p->text_addr+0x18c4d0);
            if(*call==xmb_animation_jal(xmb_animation_bind_stub))*call=xmb_animation_jal(p->text_addr+0x134194);
            for(unsigned i=0;i<2;i++)if(vt[i]==xmb_animation_dtor_stub[i])vt[i]=xmb_animation->original_destroy[i];
        }
        sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
        sceKernelCpuResumeIntr(intr);
        /* Published frames/code remain valid through prefetched calls and GE
         * lists. One bounded VSH-owned allocation, reclaimed by loadexec. */
    }else if(xmb_animation_memory>=0){
        sceKernelFreePartitionMemory(xmb_animation_memory);
        xmb_animation_memory=-1;xmb_animation=NULL;xmb_animation_mem=NULL;
    }
}
static void xmb_animation_fail(int rc) {
    controller_log("XMB animation static fallback",rc);
    xmb_animation_stop();xmb_animation_failed=1;
}
static int xmb_animation_begin(void) {
    int fd=sceIoOpen(XMB_ANIMATION_PATH,PSP_O_RDONLY,0);
    if(fd<0)return fd;
    XmbAnimationHeader h;
    int n=sceIoRead(fd,&h,sizeof(h));
    if(n!=sizeof(h) || !xmb_animation_header_valid(&h)){sceIoClose(fd);return -2;}
    unsigned bytes=h.frames*h.frame_bytes;
    SceOff end=sceIoLseek(fd,0,PSP_SEEK_END);
    if(end!=(SceOff)(sizeof(h)+bytes) || sceIoLseek(fd,sizeof(h),PSP_SEEK_SET)!=sizeof(h)){
        sceIoClose(fd);return -3;
    }
    unsigned ctxoff=(128+sizeof(xmb_animation_code)+63)&~63U;
    unsigned pixoff=(ctxoff+sizeof(XmbAnimationShared)+63)&~63U;
    if(sceKernelPartitionMaxFreeMemSize(2)<pixoff+bytes+2*1024*1024){sceIoClose(fd);return -4;}
    SceUID uid=sceKernelAllocPartitionMemory(2,"Consolizer XMB animation",PSP_SMEM_High,pixoff+bytes,NULL);
    if(uid<0){sceIoClose(fd);return uid;}
    unsigned char *mem=sceKernelGetBlockHeadAddr(uid);
    if(!mem || (uintptr_t)mem<0x08800000U ||
       !xmb_probe_range((uintptr_t)mem,pixoff+bytes,xmb_probe_ram_end(sceKernelGetModel()))){
        sceKernelFreePartitionMemory(uid);sceIoClose(fd);return -5;
    }
    memset(mem,0,pixoff);memcpy(mem+128,xmb_animation_code,sizeof(xmb_animation_code));
    XmbAnimationShared *c=(void *)(mem+ctxoff);
    c->header=h;c->pixels=(uintptr_t)(mem+pixoff);c->ram_end=xmb_probe_ram_end(sceKernelGetModel());
    c->tex_width=1;while(c->tex_width<h.width)c->tex_width<<=1;
    c->tex_height=1;while(c->tex_height<h.height)c->tex_height<<=1;
    c->u_ratio=xmb_animation_ratio(h.width,c->tex_width);
    c->v_ratio=xmb_animation_ratio(h.height,c->tex_height);
    xmb_animation_stub(mem,0,11,(uintptr_t)c,XMB_ANIMATION_LOAD_OFFSET,-1);
    xmb_animation_stub(mem,32,5,(uintptr_t)c,XMB_ANIMATION_BIND_OFFSET,-1);
    xmb_animation_stub(mem,64,5,(uintptr_t)c,XMB_ANIMATION_DESTROY_OFFSET,0);
    xmb_animation_stub(mem,96,5,(uintptr_t)c,XMB_ANIMATION_DESTROY_OFFSET,1);
    xmb_animation=c;xmb_animation_mem=mem;xmb_animation_memory=uid;
    xmb_animation_fd=fd;xmb_animation_bytes=bytes;
    xmb_animation_load_stub=(uintptr_t)mem;xmb_animation_bind_stub=(uintptr_t)(mem+32);
    xmb_animation_dtor_stub[0]=(uintptr_t)(mem+64);xmb_animation_dtor_stub[1]=(uintptr_t)(mem+96);
    return 0;
}
static int xmb_animation_install(void) {
    int intr=sceKernelCpuSuspendIntr();
    SceModule *g=sceKernelFindModuleByName("game_plugin_module");
    SceModule *p=sceKernelFindModuleByName("scePaf_Module");
    if(!g || !p){sceKernelCpuResumeIntr(intr);return 1;}
    if(!xmb_animation_game_valid(g) || !xmb_animation_paf_valid(p)){
        sceKernelCpuResumeIntr(intr);return -6;
    }
    XmbAnimationShared *c=xmb_animation;
    c->original_load=g->text_addr+0x1bda0;c->original_bind=p->text_addr+0x134194;
    c->original_destroy[0]=p->text_addr+0x13267c;c->original_destroy[1]=p->text_addr+0x1327c4;
    c->surface_vtable=p->text_addr+0x18c4d0;c->active=1;
    xmb_animation_game=g->text_addr;xmb_animation_paf=p->text_addr;
    xmb_animation_game_uid=g->modid;xmb_animation_paf_uid=p->modid;
    uint32_t *vt=(void *)c->surface_vtable;
    vt[0]=xmb_animation_dtor_stub[0];vt[1]=xmb_animation_dtor_stub[1];
    *(uint32_t *)(p->text_addr+0x132a5c)=xmb_animation_jal(xmb_animation_bind_stub);
    *(uint32_t *)(g->text_addr+0xb51c)=xmb_animation_jal(xmb_animation_load_stub);
    sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
    xmb_animation_published=1;
    sceKernelCpuResumeIntr(intr);
    controller_log("XMB animation installed; frame bank bytes",xmb_animation_bytes);
    return 0;
}
static void xmb_animation_update(unsigned long long now,int allowed) {
    if(!xmb_animation_enabled || xmb_animation_failed || xmb_probe_enabled || xmb_texture_enabled ||
       sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_VSH)return;
    if(sceKernelDevkitVersion()!=0x06060110){xmb_animation_failed=1;return;}
    if(!xmb_animation_after){xmb_animation_after=now+8000000ULL;return;}
    if(!allowed){if(xmb_animation)xmb_animation->active=0;return;}
    if(now<xmb_animation_after)return;
    if(!xmb_animation){int rc=xmb_animation_begin();if(rc<0)xmb_animation_fail(rc);return;}
    if(xmb_animation_fd>=0){
        unsigned remaining=xmb_animation_bytes-xmb_animation_loaded;
        unsigned amount=remaining>32768?32768:remaining;
        unsigned char *dest=(void *)(uintptr_t)(xmb_animation->pixels+xmb_animation_loaded);
        int n=sceIoRead(xmb_animation_fd,dest,amount);
        if(n<=0 || (unsigned)n>amount){xmb_animation_fail(-7);return;}
        for(int i=0;i<n;i++)xmb_animation_hash=(xmb_animation_hash^dest[i])*16777619U;
        xmb_animation_loaded+=n;
        if(xmb_animation_loaded==xmb_animation_bytes){
            sceIoClose(xmb_animation_fd);xmb_animation_fd=-1;
            if(xmb_animation_hash!=xmb_animation->header.data_hash){xmb_animation_fail(-8);return;}
            sceKernelDcacheWritebackAll();
        }
        return;
    }
    if(!xmb_animation_published){
        if(now<xmb_animation_poll)return;
        xmb_animation_poll=now+500000;
        int rc=xmb_animation_install();if(rc<0)xmb_animation_fail(rc);return;
    }
    if(now>=xmb_animation_poll){
        xmb_animation_poll=now+500000;
        SceModule *p=sceKernelFindModuleByUID(xmb_animation_paf_uid);
        if(!p || p->text_addr!=xmb_animation_paf){xmb_animation_fail(-9);return;}
        SceModule *g=sceKernelFindModuleByName("game_plugin_module");
        if(g && (g->modid!=xmb_animation_game_uid || g->text_addr!=xmb_animation_game)){
            int intr=sceKernelCpuSuspendIntr();
            g=sceKernelFindModuleByName("game_plugin_module");
            if(g && xmb_animation_game_valid(g)){
                xmb_animation->target=0;xmb_animation->original_load=g->text_addr+0x1bda0;
                xmb_animation_game=g->text_addr;xmb_animation_game_uid=g->modid;
                *(uint32_t *)(g->text_addr+0xb51c)=xmb_animation_jal(xmb_animation_load_stub);
                sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
            }
            sceKernelCpuResumeIntr(intr);
        }
    }
    xmb_animation->active=1;
    if(xmb_animation_last_load!=xmb_animation->loads){
        xmb_animation_last_load=xmb_animation->loads;xmb_animation_epoch=now;
        controller_log("XMB animation matching PIC1 loaded",xmb_animation_last_load);
    }
    xmb_animation->frame=(unsigned)((now-xmb_animation_epoch)/xmb_animation->header.period_us)%xmb_animation->header.frames;
    if(controller_report && xmb_animation->loads && now>=xmb_animation_log_after){
        xmb_animation_log_after=now+10000000;
        char line[128];snprintf(line,sizeof(line),"XMB animation loads=%u binds=%u destroys=%u target=%08X",
            (unsigned)xmb_animation->loads,(unsigned)xmb_animation->binds,
            (unsigned)xmb_animation->destroys,(unsigned)xmb_animation->target);
        controller_log(line,xmb_animation->frame);
    }
}
