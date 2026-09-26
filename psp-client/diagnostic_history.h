/* Only explicitly named PSPStreamer diagnostics are managed here. Settings,
 * user data and other applications' logs are never candidates for deletion. */
#include <psprtc.h>
static const char *const diagnostic_names[]={
    "PSPStreamer-watch-music.txt","PSPStreamer-watch-video.txt",
    "PSPStreamer-stream-error.txt","PSPStreamer-sync-tv.csv",
    "PSPStreamer-sync-lcd.csv"
};
static int diagnostic_name_owned(const char *name) {
    for(unsigned int i=0;i<sizeof(diagnostic_names)/sizeof(*diagnostic_names);i++) {
        size_t n=strlen(diagnostic_names[i]);
        if(strcmp(name,diagnostic_names[i])==0)return 1;
        if(strncmp(name,diagnostic_names[i],n) || strncmp(name+n,".history-",9))continue;
        const char *tail=name+n+9;
        if(strlen(tail)!=16)continue;
        int good=1;
        for(int j=0;j<16;j++)if(!((tail[j]>='0' && tail[j]<='9') || (tail[j]>='A' && tail[j]<='F')))good=0;
        if(good)return 1;
    }
    return 0;
}
static int diagnostic_clock(u64 *tick) {
    ScePspDateTime date;
    if(sceRtcGetCurrentClockLocalTime(&date)<0 || date.year<2020)return -1;
    return sceRtcGetTick(&date,tick);
}
static void diagnostic_prune(void) {
    u64 now;
    if(diagnostic_clock(&now)<0)return; /* An unset clock must not erase logs. */
    SceUID directory=sceIoDopen("ms0:/PSP/SYSTEM");
    if(directory<0)return;
    SceIoDirent entry;
    for(;;) {
        memset(&entry,0,sizeof(entry));
        if(sceIoDread(directory,&entry)<=0)break;
        u64 modified;
        if(!FIO_S_ISREG(entry.d_stat.st_mode) || !diagnostic_name_owned(entry.d_name) ||
           sceRtcGetTick(&entry.d_stat.sce_st_mtime,&modified)<0 || now<=modified ||
           now-modified<=86400000000ULL)continue;
        char path[288];snprintf(path,sizeof(path),"ms0:/PSP/SYSTEM/%s",entry.d_name);
        sceIoRemove(path);
    }
    sceIoDclose(directory);
}
static int diagnostic_rotate(const char *path) {
    SceIoStat info;
    if(sceIoGetstat(path,&info)<0)return 0;
    if(!FIO_S_ISREG(info.st_mode))return -1;
    char archive[192];u64 stamp;
    if(diagnostic_clock(&stamp)<0)stamp=(u64)sceKernelGetSystemTimeWide();
    for(int attempt=0;attempt<128;attempt++,stamp++) {
        snprintf(archive,sizeof(archive),"%s.history-%016llX",path,(unsigned long long)stamp);
        if(sceIoGetstat(archive,&info)>=0)continue;
        return sceIoRename(path,archive);
    }
    return -1; /* Never overwrite an older log if archiving failed. */
}
static void diagnostic_history_start(void) {
    diagnostic_prune();
    for(unsigned int i=0;i<sizeof(diagnostic_names)/sizeof(*diagnostic_names);i++) {
        char path[192];snprintf(path,sizeof(path),"ms0:/PSP/SYSTEM/%s",diagnostic_names[i]);
        diagnostic_rotate(path);
    }
}
