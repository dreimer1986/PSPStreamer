/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-only evidence for a GAME stalled before video presentation. No
 * thread suspension, forced recovery, memory dumps or background file handle.
 */
#include "startup_progress.h"
static void __attribute__((noinline)) controller_startup_snapshot(unsigned long long now,int pass) {
    SceGameInfo *game=sceKernelGetGameInfo();
    if(!game)return;
    char title[17]={0},path[128],line[256];
    for(unsigned i=0;i<16 && game->title_id[i];i++) {
        unsigned char c=game->title_id[i];
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return;
        title[i]=c;
    }
    if(!title[0])return;
    snprintf(path,sizeof(path),"ms0:/SEPLUGINS/PSPConsolizer/startup-%s.log",title);
    int fd=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|(pass?PSP_O_APPEND:PSP_O_TRUNC),0666);
    if(fd<0){controller_log("startup diagnostic open failed",fd);return;}
#define STARTUP_LINE(...) do { int n=snprintf(line,sizeof(line),__VA_ARGS__); \
    if(n>0)audio_probe_write_all(fd,line,(unsigned)n<sizeof(line)?(unsigned)n:sizeof(line)-1); } while(0)
    STARTUP_LINE("title=%s pass=%d time_us=%llu system=%08X USB=%d/%d poisoned=%d owner=%d\n",
        title,pass,now,sceKernelGetSystemStatus(),started,attached,poisoned,app_owner);
    STARTUP_LINE("kernel_free=%u largest=%u user_free=%u largest=%u audio_blocks=%u\n",
        sceKernelPartitionTotalFreeMemSize(1),sceKernelPartitionMaxFreeMemSize(1),
        sceKernelPartitionTotalFreeMemSize(2),sceKernelPartitionMaxFreeMemSize(2),audio_mirror?audio_mirror->blocks:0);
    SceUID ids[128];int count=0;
    int rc=sceKernelGetModuleIdList(ids,sizeof(ids),&count);
    STARTUP_LINE("modules rc=%08X count=%d cap=128\n",rc,count);
    if(rc>=0)for(int i=0;i<count && i<128;i++) {
        SceModule *m=sceKernelFindModuleByUID(ids[i]);if(!m)continue;
        STARTUP_LINE("module=%.27s uid=%08X text=%08X bytes=%u data=%u bss=%u\n",
            m->modname,(unsigned)m->modid,(unsigned)m->text_addr,(unsigned)m->text_size,(unsigned)m->data_size,(unsigned)m->bss_size);
    }
    rc=sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread,ids,128,&count);
    STARTUP_LINE("threads rc=%08X count=%d cap=128\n",rc,count);
    if(rc>=0)for(int i=0;i<count && i<128;i++) {
        SceKernelThreadInfo t={.size=sizeof(t)};
        if(sceKernelReferThreadStatus(ids[i],&t)<0)continue;
        STARTUP_LINE("thread=%.32s id=%08X entry=%08X status=%X priority=%d wait=%d id=%08X exit=%08X run=%08X:%08X\n",
            t.name,(unsigned)ids[i],(unsigned)t.entry,t.status,t.currentPriority,t.waitType,(unsigned)t.waitId,
            (unsigned)t.exitStatus,(unsigned)t.runClocks.hi,(unsigned)t.runClocks.low);
    }
#undef STARTUP_LINE
    sceIoClose(fd);controller_log("startup no frame/audio: per-title snapshot saved",pass);
}
static void controller_startup_diagnostic(unsigned long long now,int streamer) {
    static StartupProgress progress;
    static unsigned long long next;
    static int finished,pass;
    if(finished || !controller_report || controller_suspended || controller_disabled)return;
    if(streamer || sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME){finished=1;return;}
    int stalled=startup_audio_stalled(&progress,now,audio_mirror?audio_mirror->blocks:0);
    if(!pass && !stalled)return;
    if(now<next)return;
    void *frame=NULL;int stride=0,format=0;
    int rc=sceDisplayGetFrameBuf(&frame,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE);
    if(rc<0)return;
    /* A short startup sound does not prove that the game started. Require
     * sustained lack of audio progress, not a lifetime block count of zero.
     * Once triggered, take the second snapshot even if output recovered. */
    if(!pass && frame){finished=1;return;}
    controller_startup_snapshot(now,pass++);
    next=now+2000000;
    if(pass==2)finished=1;
}
