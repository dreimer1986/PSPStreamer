#ifndef PSPSTREAMER_OC_SETTINGS_IO_H
#define PSPSTREAMER_OC_SETTINGS_IO_H
#include <errno.h>
#include <stdio.h>
#include "../psp-overclock/config_parse.h"
/* Startup-only configuration, never live hardware control. */
static int oc_settings_read(const char *path,OcConfig *config) {
    char data[2049];int keys,line;
    FILE *f=fopen(path,"rb");
    if(!f) {
        if(errno!=ENOENT)return -1;
        strcpy(data,"enabled=0");
        return oc_config_parse(data,(int)strlen(data),config,&keys,&line)?-1:1;
    }
    size_t n=fread(data,1,sizeof(data)-1,f);int failed=ferror(f),extra=fgetc(f);
    if(fclose(f))failed=1;
    if(failed || extra!=EOF)return -1;
    data[n]=0;return oc_config_parse(data,(int)n,config,&keys,&line);
}
static int oc_settings_write(const char *path,const OcConfig *c) {
    char data[512],check[512],temp[192],backup[192];OcConfig verified;int keys,line;
    int n=snprintf(data,sizeof(data),"# StreamerOC - applied on next application start\n"
        "enabled=%d\ntarget_mhz=%d\nenforce=%d\nenforce_unlimited=%d\napp_control=%d\nreport=%d\noverlay=%d\n",
        c->enabled,c->target,c->enforce,c->enforce_unlimited,c->app_control,c->report,c->overlay);
    if(n<0 || n>=(int)sizeof(data))return -1;
    strcpy(check,data);if(oc_config_parse(check,n,&verified,&keys,&line))return -1;
    if(snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp) ||
       snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup))return -1;
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    int failed=fwrite(data,1,n,f)!=(size_t)n;
    if(fflush(f))failed=1;
    if(fclose(f))failed=1;
    if(failed)return -1;
    f=fopen(path,"rb");int existed=f!=NULL;
    if(f)fclose(f);else if(errno!=ENOENT)return -1;
    if(existed) {
        if(remove(backup) && errno!=ENOENT)return -1;
        if(rename(path,backup))return -1;
    }
    if(rename(temp,path)) {if(existed)rename(backup,path);return -1;}
    return 0;
}
#endif
