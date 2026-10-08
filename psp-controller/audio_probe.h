/* SPDX-License-Identifier: GPL-2.0-or-later
 * Phase 1, READ-ONLY runtime reconnaissance for an optional PCM mirror.
 * No driver patches, DMA reads, audio reservation, USB traffic or new thread.
 * uOFW is a reference, NOT an assumed signature for a running 6.61 driver.
 */
#include "audio_probe_stats.h"
static int audio_probe_enabled;
static int audio_probe_started,audio_probe_finished;
static unsigned long long audio_probe_start,audio_probe_next,audio_probe_report;
static int (*audio_probe_normal)(unsigned);
static int (*audio_probe_src)(void);
static AudioProbeStats audio_probe_stats;
static char audio_probe_path[128];

static int audio_probe_write_all(int fd,const void *data,unsigned size) {
    const unsigned char *p=data;
    while(size) {int n=sceIoWrite(fd,p,size);if(n<=0 || (unsigned)n>size)return -1;p+=n;size-=n;}
    return 0;
}
static void audio_probe_line(const char *line) {
    if(!controller_report || !audio_probe_path[0])return;
    int f=sceIoOpen(audio_probe_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(f<0){audio_probe_finished=1;controller_log("audio probe log open failed",f);return;}
    int rc=audio_probe_write_all(f,line,strlen(line));sceIoClose(f);
    if(rc<0){audio_probe_finished=1;controller_log("audio probe log write failed",rc);}
}
static unsigned audio_probe_export(const char *library,unsigned nid,const char *name) {
    unsigned ptr=(unsigned)sctrlHENFindFunction("sceAudio_Driver",library,nid);
    char line[160];snprintf(line,sizeof(line),"export %s %s nid=%08X address=%08X\n",library,name,nid,ptr);
    audio_probe_line(line);return ptr;
}
static void audio_probe_begin(void) {
    audio_probe_started=1;
    char key[20]="GAME",line[256],binary[128];
    int context=sceKernelInitKeyConfig();
    if(context==PSP_INIT_KEYCONFIG_VSH)strcpy(key,"VSH");
    else if(context==PSP_INIT_KEYCONFIG_POPS)strcpy(key,"POPS");
    SceGameInfo *game=sceKernelGetGameInfo();
    if(context!=PSP_INIT_KEYCONFIG_VSH && game && game->title_id[0]) {
        unsigned n=0;
        for(unsigned i=0;i<sizeof(game->title_id) && n<sizeof(key)-1;i++) {
            unsigned char c=game->title_id[i];if(!c)break;
            if((c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='-')key[n++]=c;
        }
        if(n)key[n]=0;
    }
    snprintf(audio_probe_path,sizeof(audio_probe_path),"ms0:/SEPLUGINS/PSPConsolizer/audio-probe-%s.log",key);
    snprintf(binary,sizeof(binary),"ms0:/SEPLUGINS/PSPConsolizer/audio-probe-%s-text.bin",key);
    /* Per-title names keep a return to XMB from overwriting the game's evidence.
     * Bound storage to one current pair and one previous pair per tested title. */
    const char *paths[]={audio_probe_path,binary};
    for(unsigned i=0;i<2;i++) {
        int old=sceIoOpen(paths[i],PSP_O_RDONLY,0);
        if(old>=0) {char previous[144];sceIoClose(old);snprintf(previous,sizeof(previous),"%s.previous",paths[i]);
            sceIoRemove(previous);sceIoRename(paths[i],previous);}
    }
    int f=sceIoOpen(audio_probe_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
    if(f<0){audio_probe_finished=1;controller_log("audio probe create failed",f);return;}sceIoClose(f);
    snprintf(line,sizeof(line),"PSPConsolizer audio probe v1 READ ONLY\nfirmware=%08X context=%d key=%s\n",
             (unsigned)sceKernelDevkitVersion(),context,key);audio_probe_line(line);
    SceModule *mod=sceKernelFindModuleByName("sceAudio_Driver");
    if(!mod || !audio_probe_text_valid(mod->text_addr,mod->text_size,mod->segmentaddr,mod->segmentsize,mod->nsegment)) {
        audio_probe_line("Audio module absent or text range not validated; probe stopped.\n");audio_probe_finished=1;return;
    }
    snprintf(line,sizeof(line),"module=%s uid=%08X version=%u.%u text=%08X size=%u data_size=%u bss_size=%u\n",
             mod->modname,(unsigned)mod->modid,mod->version[1],mod->version[0],(unsigned)mod->text_addr,(unsigned)mod->text_size,(unsigned)mod->data_size,(unsigned)mod->bss_size);
    audio_probe_line(line);
    for(unsigned i=0;i<mod->nsegment;i++) {
        snprintf(line,sizeof(line),"segment%u address=%08X size=%u\n",i,(unsigned)mod->segmentaddr[i],mod->segmentsize[i]);audio_probe_line(line);
    }
    unsigned normal=audio_probe_export("sceAudio_driver",0x9D77949E,"GetChannelRestLength");
    unsigned src=audio_probe_export("sceAudio_driver",0x8A7CD9C6,"Output2GetRestSample");
    if(normal>=mod->text_addr && normal-mod->text_addr<mod->text_size && !(normal&3))audio_probe_normal=(void *)normal;
    if(src>=mod->text_addr && src-mod->text_addr<mod->text_size && !(src&3))audio_probe_src=(void *)src;
    audio_probe_export("sceAudio_driver",0x4A0FE97D,"SetFrequency");
    audio_probe_export("sceAudio_driver",0xAC81DE4F,"SRCOutputBlocking");
    audio_probe_export("sceAudio_driver",0x5CDEF9A4,"OutputBlocking");
    audio_probe_export("sceAudio_driver",0x837701CC,"SRCChReserve");
    SceUID ids[64];int count=0;
    int result=sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread,ids,64,&count);
    snprintf(line,sizeof(line),"thread_list rc=%08X count=%d (inspection capped at 64)\n",(unsigned)result,count);audio_probe_line(line);
    if(result>=0)for(int i=0;i<count && i<64;i++) {
        SceKernelThreadInfo info={.size=sizeof(info)};
        if(sceKernelReferThreadStatus(ids[i],&info)>=0 && strstr(info.name,"Audio")) {
            snprintf(line,sizeof(line),"thread=%.32s id=%08X entry=%08X priority=%d stack=%u wait=%d\n",
                     info.name,(unsigned)ids[i],(unsigned)info.entry,info.currentPriority,info.stackSize,info.waitType);audio_probe_line(line);
        }
    }
    /* Dump ONLY this module's validated TEXT, never user PCM/general RAM/BSS.
     * No allocation: write small chunks directly from resident driver code. */
    f=sceIoOpen(binary,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
    unsigned bytes=0;uint32_t hash=2166136261U;
    if(f>=0) {
        const unsigned char *p=(const unsigned char *)mod->text_addr;
        while(bytes<mod->text_size) {
            unsigned size=mod->text_size-bytes;if(size>1024)size=1024;
            if(audio_probe_write_all(f,p+bytes,size)<0)break;
            for(unsigned i=0;i<size;i++)hash=(hash^p[bytes+i])*16777619U;
            bytes+=size;sceKernelDelayThread(1000);
        }
        sceIoClose(f);
    }
    snprintf(line,sizeof(line),"text_dump bytes=%u expected=%u fnv1a=%08X open_rc=%08X\n",bytes,(unsigned)mod->text_size,(unsigned)hash,(unsigned)f);audio_probe_line(line);
    audio_probe_line("Sampling queue occupancy for 120 s at >=50 ms intervals. Zero hits do NOT prove an unused path. No PCM output yet.\n");
    audio_probe_start=sceKernelGetSystemTimeWide();audio_probe_next=audio_probe_start;audio_probe_report=audio_probe_start+5000000ULL;
    controller_log("read-only audio probe started",normal && src?0:-1);
}
static void audio_probe_summary(unsigned long long now,const char *reason) {
    char line[256];AudioProbeStats *s=&audio_probe_stats;
    snprintf(line,sizeof(line),"t_ms=%u %s samples=%u normal_hits=%u,%u,%u,%u,%u,%u,%u,%u src_hits=%u src_empty=%u src_unreserved=%u\n",
        (unsigned)((now-audio_probe_start)/1000),reason,s->samples,s->normal_hits[0],s->normal_hits[1],s->normal_hits[2],s->normal_hits[3],
        s->normal_hits[4],s->normal_hits[5],s->normal_hits[6],s->normal_hits[7],s->src_hits,s->src_empty,s->src_unreserved);audio_probe_line(line);
    snprintf(line,sizeof(line),"peaks=%u,%u,%u,%u,%u,%u,%u,%u src_peak=%u normal_errors=%u/%08X src_errors=%u/%08X\n",
        s->normal_peak[0],s->normal_peak[1],s->normal_peak[2],s->normal_peak[3],s->normal_peak[4],s->normal_peak[5],s->normal_peak[6],s->normal_peak[7],
        s->src_peak,s->normal_errors,(unsigned)s->normal_last_error,s->src_errors,(unsigned)s->src_last_error);audio_probe_line(line);
}
static void audio_probe_update(unsigned long long now,int allowed) {
    if(!audio_probe_enabled || !controller_report || audio_probe_finished)return;
    if(!allowed) {
        if(audio_probe_started) {audio_probe_summary(now,"stopped: context/ownership/power");audio_probe_finished=1;}
        return;
    }
    if(!audio_probe_started){audio_probe_begin();return;}
    if(now-audio_probe_start>=120000000ULL) {
        audio_probe_summary(now,"complete");audio_probe_finished=1;return;
    }
    if(now<audio_probe_next)return;
    audio_probe_next=now+50000;
    int values[8];for(unsigned i=0;i<8;i++)values[i]=audio_probe_normal?audio_probe_normal(i):-1;
    audio_probe_observe(&audio_probe_stats,values,audio_probe_src?audio_probe_src():-1);
    if(now>=audio_probe_report) {audio_probe_summary(now,"interval");audio_probe_report=now+5000000ULL;}
}
static void audio_probe_stop(void) {
    if(audio_probe_started && !audio_probe_finished){audio_probe_summary(sceKernelGetSystemTimeWide(),"plugin stopping");audio_probe_finished=1;}
}
