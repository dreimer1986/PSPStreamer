/* Low-frequency, closed writes also cover preparation before the playback
 * watchdog exists. Never include URLs, media IDs or authentication headers. */
static void recovery_log(const char *event,int result,int status,const char *stage) {
    if(!debug_enabled)return;
    char line[256];
    int n=snprintf(line,sizeof(line),"tick_ms=%llu event=%s result=%d http=%d stage=%s\n",
        (unsigned long long)sceKernelGetSystemTimeWide()/1000ULL,event,result,status,stage);
    if(n<0 || n>=(int)sizeof(line))return;
    SceUID fd=sceIoOpen("ms0:/PSP/SYSTEM/PSPStreamer-recovery.txt",
        PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0600);
    if(fd<0)return;
    sceIoWrite(fd,line,n);sceIoClose(fd);
}
