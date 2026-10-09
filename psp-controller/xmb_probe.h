/* SPDX-License-Identifier: GPL-2.0-or-later
 * Opt-in read-only VSH module capture for the animated PIC1 investigation.
 * No hooks, guessed offsets, texture writes, flash writes or codec loading.
 * Incremental worker I/O, never from a presenter/USB/audio callback.
 */
#include "xmb_probe_bounds.h"
static int xmb_probe_enabled;
static SceUID xmb_probe_memory=-1,xmb_probe_fd=-1,xmb_probe_log=-1;
static unsigned char *xmb_probe_buffer;
static unsigned long long xmb_probe_after,xmb_probe_deadline,xmb_probe_next;
static int xmb_probe_begun,xmb_probe_finished,xmb_probe_slot=-1;
static unsigned xmb_probe_done,xmb_probe_segment,xmb_probe_offset,xmb_probe_crc;
static SceModule xmb_probe_module;
static char xmb_probe_partial[168],xmb_probe_final[160];
static const char *const xmb_probe_names[]={"scePaf_Module","game_plugin_module","vsh_module"};
#define XMB_PROBE_DIR "ms0:/SEPLUGINS/PSPConsolizer/xmb-probe"

static int xmb_probe_write(int fd,const void *data,unsigned bytes) {
    const unsigned char *p=data;
    while(bytes){int n=sceIoWrite(fd,p,bytes);if(n<=0 || (unsigned)n>bytes)return -1;p+=n;bytes-=n;}
    return 0;
}
static void xmb_probe_finish(const char *reason) {
    if(xmb_probe_fd>=0){sceIoClose(xmb_probe_fd);xmb_probe_fd=-1;}
    if(xmb_probe_log>=0){
        char line[160];int n=snprintf(line,sizeof(line),"finish=%s completed_mask=%u\n",reason,xmb_probe_done);
        xmb_probe_write(xmb_probe_log,line,n);sceIoClose(xmb_probe_log);xmb_probe_log=-1;
    }
    if(xmb_probe_memory>=0){sceKernelFreePartitionMemory(xmb_probe_memory);xmb_probe_memory=-1;xmb_probe_buffer=NULL;}
    xmb_probe_finished=1;
    controller_log("XMB read-only capture finished",(int)xmb_probe_done);
}
static int xmb_probe_record(const char *line) {
    if(xmb_probe_write(xmb_probe_log,line,strlen(line))<0){xmb_probe_finish("log_write_error");return -1;}
    return 0;
}
static void xmb_probe_start(void) {
    if(!xmb_probe_enabled || !controller_report || sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_VSH)return;
    xmb_probe_after=sceKernelGetSystemTimeWide()+8000000ULL;
}
static void xmb_probe_update(unsigned long long now,int allowed) {
    if(!xmb_probe_after || xmb_probe_finished)return;
    if(!allowed){if(xmb_probe_begun)xmb_probe_finish("suspend_usb_or_disabled");return;}
    if(now<xmb_probe_after || now<xmb_probe_next)return;
    xmb_probe_next=now+20000;
    if(!xmb_probe_begun){
        xmb_probe_begun=1;xmb_probe_deadline=now+120000000ULL;
        xmb_probe_memory=sceKernelAllocPartitionMemory(2,"Consolizer XMB probe",PSP_SMEM_High,8192,NULL);
        if(xmb_probe_memory<0){xmb_probe_finish("no_user_ram");return;}
        xmb_probe_buffer=sceKernelGetBlockHeadAddr(xmb_probe_memory);
        if(!xmb_probe_buffer){xmb_probe_finish("no_user_address");return;}
        sceIoMkdir(XMB_PROBE_DIR,0777);
        xmb_probe_log=sceIoOpen(XMB_PROBE_DIR "/capture.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
        if(xmb_probe_log<0){xmb_probe_finish("open_error");return;}
        char line[192];snprintf(line,sizeof(line),"PSPConsolizer XMB probe v1 READ ONLY\nfirmware=%08X model=%d tick=%llu\nNo animation renderer installed. Private diagnostic module capture.\n",
            (unsigned)sceKernelDevkitVersion(),sceKernelGetModel(),now);
        if(xmb_probe_record(line)<0)return;
        controller_log("XMB capture started; select PSPStreamer in XMB",0);
    }
    if(now>=xmb_probe_deadline){xmb_probe_finish("timeout");return;}
    if(xmb_probe_slot<0){
        /* Module snapshots are copied with scheduling/interrupts excluded.
         * Each later chunk revalidates identity and segment layout before
         * touching the range; a menu unload cancels that capture. */
        int intr=sceKernelCpuSuspendIntr();
        for(unsigned i=0;i<3;i++)if(!(xmb_probe_done&(1U<<i))){
            SceModule *m=sceKernelFindModuleByName(xmb_probe_names[i]);
            if(m){memcpy(&xmb_probe_module,m,sizeof(*m));xmb_probe_slot=(int)i;break;}
        }
        sceKernelCpuResumeIntr(intr);
        if(xmb_probe_slot<0){xmb_probe_next=now+500000;return;}
        SceModule *m=&xmb_probe_module;
        unsigned total=0;int valid=m->nsegment>0 && m->nsegment<=4;
        for(unsigned i=0;valid && i<m->nsegment;i++){
            if(!xmb_probe_range(m->segmentaddr[i],m->segmentsize[i]) ||
               m->segmentsize[i]>8U*1024*1024-total)valid=0;
            else total+=m->segmentsize[i];
        }
        if(!valid){xmb_probe_finish("invalid_module_layout");return;}
        char line[256];snprintf(line,sizeof(line),"module=%s uid=%08X text=%08X text_size=%u data=%u bss=%u segments=%u exports=%08X export_size=%u imports=%08X import_size=%u\n",
            xmb_probe_names[xmb_probe_slot],(unsigned)m->modid,(unsigned)m->text_addr,(unsigned)m->text_size,
            (unsigned)m->data_size,(unsigned)m->bss_size,m->nsegment,(unsigned)m->ent_top,(unsigned)m->ent_size,
            (unsigned)m->stub_top,(unsigned)m->stub_size);
        if(xmb_probe_record(line)<0)return;
        xmb_probe_segment=0;xmb_probe_offset=0;
    }
    unsigned segment=xmb_probe_segment;
    if(xmb_probe_fd<0){
        snprintf(xmb_probe_final,sizeof(xmb_probe_final),XMB_PROBE_DIR "/%s-%u.bin",xmb_probe_names[xmb_probe_slot],segment);
        snprintf(xmb_probe_partial,sizeof(xmb_probe_partial),"%s.part",xmb_probe_final);
        xmb_probe_fd=sceIoOpen(xmb_probe_partial,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
        if(xmb_probe_fd<0){xmb_probe_finish("segment_open_error");return;}
        xmb_probe_crc=2166136261U;
    }
    unsigned remaining=xmb_probe_module.segmentsize[segment]-xmb_probe_offset;
    unsigned amount=remaining<8192?remaining:8192;
    int intr=sceKernelCpuSuspendIntr();
    SceModule *m=sceKernelFindModuleByName(xmb_probe_names[xmb_probe_slot]);
    int valid=m && m->modid==xmb_probe_module.modid && m->nsegment==xmb_probe_module.nsegment &&
        m->segmentaddr[segment]==xmb_probe_module.segmentaddr[segment] &&
        m->segmentsize[segment]==xmb_probe_module.segmentsize[segment];
    if(valid)memcpy(xmb_probe_buffer,(void *)(xmb_probe_module.segmentaddr[segment]+xmb_probe_offset),amount);
    sceKernelCpuResumeIntr(intr);
    if(!valid){xmb_probe_finish("module_unloaded_or_changed");return;}
    if(xmb_probe_write(xmb_probe_fd,xmb_probe_buffer,amount)<0){xmb_probe_finish("segment_write_error");return;}
    for(unsigned i=0;i<amount;i++)xmb_probe_crc=(xmb_probe_crc^xmb_probe_buffer[i])*16777619U;
    xmb_probe_offset+=amount;
    if(xmb_probe_offset==xmb_probe_module.segmentsize[segment]){
        int rc=sceIoClose(xmb_probe_fd);xmb_probe_fd=-1;
        if(rc<0){xmb_probe_finish("segment_close_error");return;}
        /* Replace only this diagnostic's own completed file, never a plugin. */
        sceIoRemove(xmb_probe_final);
        if(sceIoRename(xmb_probe_partial,xmb_probe_final)<0){xmb_probe_finish("segment_rename_error");return;}
        char line[192];snprintf(line,sizeof(line),"segment=%s-%u.bin address=%08X size=%u fnv1a=%08X complete=1\n",
            xmb_probe_names[xmb_probe_slot],segment,(unsigned)xmb_probe_module.segmentaddr[segment],xmb_probe_offset,xmb_probe_crc);
        if(xmb_probe_record(line)<0)return;
        xmb_probe_offset=0;
        if(++xmb_probe_segment==xmb_probe_module.nsegment){
            xmb_probe_done|=1U<<xmb_probe_slot;xmb_probe_slot=-1;
            if(xmb_probe_done==7)xmb_probe_finish("complete");
        }
    }
}
static void xmb_probe_stop(void) {
    if(xmb_probe_begun && !xmb_probe_finished)xmb_probe_finish("service_exit");
}
