#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <unistd.h>
#include "../psp-client/plugin_settings_io.h"
static void content(PluginIni *d,const char *s){strcpy(d->text,s);d->length=strlen(s);}
int main(void){
    char dir[]="/tmp/plugin-settings-XXXXXX",path[192],backup[208];assert(mkdtemp(dir));
    snprintf(path,sizeof(path),"%s/plugin.ini",dir);PluginIni d;assert(!pi_open(&d,path,2047));
    const char *original="# preserved\r\nenabled=1\r\nunknown=keep\r\noverlay_always=1\r\nenabled=0\r\n";
    content(&d,original);assert(!pi_save(&d));assert(pi_get(&d,-1,"enabled",-1)==0);
    assert(!pi_set(&d,-1,"enabled",1));assert(strstr(d.text,"# preserved\r\n"));assert(strstr(d.text,"unknown=keep\r\n"));
    assert(!strstr(d.text,"enabled=0"));assert(pi_get(&d,-1,"overlay_always",0)==1);assert(!pi_save(&d));
    snprintf(backup,sizeof(backup),"%s.bak",path);FILE *f=fopen(backup,"rb");assert(f);char b[256]={0};assert(fread(b,1,sizeof(b)-1,f)==strlen(original));fclose(f);assert(!strcmp(b,original));
    PluginIni reloaded;assert(!pi_open(&reloaded,path,2047));assert(!strcmp(d.text,reloaded.text));free(reloaded.text);
    d.limit=65536;content(&d,"\xef\xbb\xbf[title:ULUS12345]\r\n# keep\r\nenabled=1\r\n[path:ms0:/PSP/GAME/A/EBOOT.PBP]\r\nenabled=0\r\n");
    TitleRuleKey keys[]={{"enabled",0,1},{"target_mhz",66,471}};
    assert(!pi_validate_rules(&d,keys,2));assert(!pi_set(&d,0,"target_mhz",333));
    assert(pi_get(&d,0,"target_mhz",-1)==333);assert(pi_get(&d,1,"enabled",-1)==0);
    assert(!pi_set(&d,0,"enabled",-1));assert(pi_get(&d,0,"enabled",-1)==-1);assert(strstr(d.text,"# keep"));assert(!pi_validate_rules(&d,keys,2));
    size_t a,end;char name[384];assert(!pi_section(&d,1,&a,&end,name));assert(!strcmp(name,"[path:ms0:/PSP/GAME/A/EBOOT.PBP]"));
    assert(!pi_replace(&d,a,end,""));assert(pi_section(&d,1,&a,&end,NULL)<0);
    content(&d,"[title:ULUS12345]\nenabled=2\n");assert(pi_validate_rules(&d,keys,2)<0);
    content(&d,"[title:ULUS12345]\nunknown=1\n");assert(pi_validate_rules(&d,keys,2)<0);
    content(&d,"enabled=1");d.limit=d.length;assert(pi_set(&d,-1,"enabled",0)<0);assert(!strcmp(d.text,"enabled=1"));
    d.limit=2047;assert(!pi_set(&d,-1,"target_mhz",333));assert(strstr(d.text,"enabled=1\ntarget_mhz=333\n"));
    free(d.text);assert(pi_open(&d,path,1)<0);
    unlink(path);unlink(backup);rmdir(dir);puts("Plugin settings: preservation, scope, inheritance, limits and verified backup OK");return 0;
}
