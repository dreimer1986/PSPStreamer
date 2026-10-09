/* SPDX-License-Identifier: GPL-2.0-or-later
 * Loader only: load Sony USB before resolving the shared bridge imports. */
#include <pspkernel.h>
#include <psploadcore.h>
#include <pspinit.h>
#include <systemctrl.h>
#include <stdio.h>
#include "report_config.h"
#include "vsh_audio_start.h"
#include "audio_probe_stats.h"
#include "audio_mirror_signature.h"
#include "pops_audio_link.h"
#include "../psp-overclock/title_rules_io.h"
PSP_MODULE_INFO("PSPConsolizer",PSP_MODULE_KERNEL,0,2);
static SceUID worker=-1;
static int report_enabled=1;
static int fast_vsh_audio;
static int early_status=-27,early_requested,early_client,early_stopping;
static unsigned long long early_time;
static char early_message[96];
static void pops_audio_boot_log(const char *text,int rc) {
    (void)rc;snprintf(early_message,sizeof(early_message),"%s",text);
}
#include "pops_audio_bootstrap.h"
static STMOD_HANDLER early_previous;
static int early_handler_set,early_handler_pinned;
static void early_try(SceModule *mod) {
    if(!early_requested || pops_audio_setup || (early_status<0 && early_status!=-20))return;
    SceModule *m=mod && !strcmp(mod->modname,"scePops_Manager")?mod:
        sceKernelFindModuleByName("scePops_Manager");
    if(!m)return;
    early_time=sceKernelGetSystemTimeWide();early_status=pops_audio_install(m);
}
static int early_module_start(SceModule *mod) {
    int rc=early_previous?early_previous(mod):0;
    if(!strcmp(mod->modname,"pops") || !strcmp(mod->modname,"scePops_Manager"))early_try(mod);
    return rc;
}
static void early_detach(void) {
    if(!early_handler_set)return;
    int intr=sceKernelCpuSuspendIntr();
    STMOD_HANDLER current=sctrlHENSetStartModuleHandler(early_previous);
    if(current!=early_module_start) {
        /* A later handler may chain through us. Keep our code resident. */
        sctrlHENSetStartModuleHandler(current);early_handler_pinned=1;
    } else early_handler_set=0;
    sceKernelCpuResumeIntr(intr);
}
int consolizerAudioService(unsigned op,PopsAudioLink *link) {
    if(!link || link->abi!=POPS_AUDIO_ABI || op>2)return -1;
    int intr=sceKernelCpuSuspendIntr();
    if(early_stopping){sceKernelCpuResumeIntr(intr);return -1;}
    if(op==1) {
        if(early_client){sceKernelCpuResumeIntr(intr);return -1;}
        early_client=1;
    } else if(op==2)early_client=0;
    link->shared=pops_audio_shared;link->published=pops_audio_published;
    /* A pre-start layout can still be incomplete. Keep the consumer attached
     * while the chained handler waits for another module-start event. */
    link->status=early_status==-20 && early_handler_set && !early_stopping?0:early_status;
    sceKernelCpuResumeIntr(intr);return 0;
}
static void early_configure(const char *config) {
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_POPS)return;
    int enabled=1,pops=1,mirror=0,excluded=0,allow_seen=0,allow_match=0,exclude_seen=0;
    const char *path=sceKernelInitFileName();if(!path)path="";
    /* Match the service's exact boolean keys; unknown lines remain ignored. */
    char line[128];const char *s=config;
    while(*s) {
        const char *end=strchr(s,'\n');if(!end)end=s+strlen(s);
        while(s<end && (*s==' ' || *s=='\t'))s++;
        size_t n=end-s;while(n && (s[n-1]=='\r' || s[n-1]==' ' || s[n-1]=='\t'))n--;
        if(n<sizeof(line)) {
            memcpy(line,s,n);line[n]=0;
            if(!strncmp(line,"enabled=",8))enabled=!strcmp(line+8,"1");
            else if(!strncmp(line,"pops=",5))pops=!strcmp(line+5,"1");
            else if(!strncmp(line,"audio_mirror=",13))mirror=!strcmp(line+13,"1");
            else if(!strncmp(line,"exclude_path=",13) && line[13] && exclude_seen<8) {
                line[13+95]=0;exclude_seen++;if(strstr(path,line+13))excluded=1;
            } else if(!strncmp(line,"allow_path=",11) && line[11] && allow_seen<8) {
                line[11+95]=0;allow_seen++;if(strstr(path,line+11))allow_match=1;
            }
        }
        s=*end?end+1:end;
    }
    if(!pops || !mirror || excluded || (allow_seen && !allow_match))return;
    static const TitleRuleKey keys[]={{"enabled",0,1},{"home_combo",0,1},{"overlay",0,2},
        {"overlay_always",0,1},{"tvout",0,2},{"metadata",0,1},{"pops_rumble",0,1}};
    int values[]={enabled,0,0,0,0,0,0};
    if(title_rules_load("ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer-rules.ini",
        sceKernelInitFileName(),keys,7,values)<0 || !values[0])return;
    early_requested=1;early_status=0;
    early_previous=sctrlHENSetStartModuleHandler(early_module_start);early_handler_set=1;
    early_try(NULL);
}
static void report(const char *what,int rc) {
    if(!report_enabled)return;
    char line[120];int n=snprintf(line,sizeof(line),"%s: %08X\n",what,(unsigned)rc);
    int fd=sceIoOpen("ms0:/SEPLUGINS/PSPConsolizer/loader.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(fd>=0){sceIoWrite(fd,line,n);sceIoClose(fd);}
    if(sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_POPS) {
        fd=sceIoOpen("ms0:/SEPLUGINS/PSPConsolizer/loader-pops.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
        if(fd>=0){sceIoWrite(fd,line,n);sceIoClose(fd);}
    }
}
static int load_start(const char *path) {
    int m=sceKernelLoadModule(path,0,NULL),status=0;
    if(m<0)return m;
    int rc=sceKernelStartModule(m,0,NULL,&status,NULL);
    if(rc<0 || status<0){sceKernelUnloadModule(m);return rc<0?rc:status;}
    return 0;
}
static int start_worker(SceSize size,void *args) {
    (void)size;(void)args;
    /* Do not spend two seconds of the boot sound waiting if VSH's audio,
     * USB and input services are already resident. Keep the established
     * fallback, and all GAME/POPS startup timing, unchanged. */
    int early_vsh=fast_vsh_audio && sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_VSH &&
        sceKernelFindModuleByName("sceAudio_Driver") &&
        sceKernelFindModuleByName("sceUSB_Driver") &&
        sceKernelFindModuleByName("sceController_Service") &&
        sceKernelFindModuleByName("sceDisplay_Service");
    if(!early_vsh)sceKernelDelayThread(2000000);
    /* Keep the last PS1 run even if a file manager starts before USB mode. */
    if(report_enabled && sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_POPS) {
        const char *pops_logs[]={"ms0:/SEPLUGINS/PSPConsolizer/loader-pops.log",
            "ms0:/SEPLUGINS/PSPConsolizer/last-pops.log"};
        for(unsigned i=0;i<2;i++) {
            int f=sceIoOpen(pops_logs[i],PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);
            if(f>=0)sceIoClose(f);
        }
    }
    const char *logs[]={"ms0:/SEPLUGINS/PSPConsolizer/loader.log","ms0:/SEPLUGINS/PSPConsolizer/last.log"};
    if(report_enabled)for(unsigned i=0;i<2;i++){
        char previous[128];snprintf(previous,sizeof(previous),"%s.previous",logs[i]);
        /* Preserve one preceding launch, notably cold VSH before a game. */
        int old=sceIoOpen(logs[i],PSP_O_RDONLY,0);
        if(old>=0){sceIoClose(old);sceIoRemove(previous);sceIoRename(logs[i],previous);}
        int f=sceIoOpen(logs[i],PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0666);if(f>=0)sceIoClose(f);
    }
    if(early_requested) {
        report("early POPS install status",early_status);
        report("early POPS install time us",(int)early_time);
        report("POPS layout rejection bits",pops_audio_layout);
        report("POPS firmware",pops_audio_fw);
        const char *fields[]={"manager text","manager text size","manager segments",
            "manager data size","manager BSS size","manager segment 0",
            "manager segment 0 size","manager segment 1","manager segment 1 size"};
        for(unsigned i=0;i<9;i++)report(fields[i],pops_audio_manager_meta[i]);
        report("POPS setup callback",pops_audio_callback);
        report("POPS callback module text",pops_audio_pops_text);
        report("POPS callback module size",pops_audio_pops_size);
        if(early_message[0])report(early_message,pops_audio_published);
        if(pops_audio_setup || (early_status<0 && early_status!=-20))early_detach();
    }
    report("VSH audio ready-services fast start",early_vsh);
    int rc=0;
    if(!sceKernelFindModuleByName("sceUSB_Driver")) {
        rc=load_start("flash0:/kd/usb.prx");
        if((unsigned)rc==0x80020139U)rc=0;
    }
    report("Sony USB",rc);
    if(rc>=0 && !sceKernelFindModuleByName("PSPConsolizerUSB") && !sceKernelFindModuleByName("StreamMasterUSB"))
        rc=load_start("ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizerUSB.prx");
    report("shared bridge",rc);return 0;
}
int module_start(SceSize size,void *args) {
    (void)size;(void)args;
    static char config[2048];
    int config_fd=sceIoOpen("ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer.ini",PSP_O_RDONLY,0);
    if(config_fd>=0) {
        int n=sceIoRead(config_fd,config,sizeof(config)-1);sceIoClose(config_fd);
        if(n>0){config[n]=0;report_enabled=consolizer_report_setting(config);
            fast_vsh_audio=consolizer_vsh_audio_requested(config);early_configure(config);}
    }
    worker=sceKernelCreateThread("PSPConsolizer loader",start_worker,0x30,4096,0,NULL);
    /* Once an early hook exists, returning an error could unload its code.
     * Keep the loader resident even if USB worker creation fails. */
    if(worker<0){report("loader worker create failed",worker);return early_requested?0:worker;}
    int rc=sceKernelStartThread(worker,0,NULL);
    if(rc<0){sceKernelDeleteThread(worker);worker=-1;report("loader worker start failed",rc);}
    return early_requested?0:rc;
}
int module_stop(SceSize size,void *args) {
    (void)size;(void)args;
    if(worker>=0){SceUInt wait=100000;if(sceKernelWaitThreadEnd(worker,&wait)<0)return -1;sceKernelDeleteThread(worker);worker=-1;}
    int intr=sceKernelCpuSuspendIntr();
    if(early_client){sceKernelCpuResumeIntr(intr);return -1;}
    early_stopping=1;early_requested=0;
    sceKernelCpuResumeIntr(intr);
    early_detach();pops_audio_uninstall();
    if(early_handler_pinned || pops_audio_pinned)return -1;
    return 0;
}
