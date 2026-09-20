#include <assert.h>
#include <sys/stat.h>
#include "oc_settings_io.h"
static void put(const char *path,const char *data) {
    FILE *f=fopen(path,"wb");assert(f);assert(fputs(data,f)>=0);assert(!fclose(f));
}
int main(void) {
    const char *path="StreamerOC.ini";OcConfig c,original;
    assert(oc_settings_read(path,&c)==1 && c.enabled==0 && c.target==333);
    assert(c.app_control && c.report && c.overlay && !c.enforce_unlimited);
    assert(!oc_settings_write(path,&c));original=c;
    c.enabled=1;c.target=433;c.enforce=1;c.enforce_unlimited=1;
    assert(!oc_settings_write(path,&c));
    assert(!oc_settings_read("StreamerOC.ini.bak",&original));
    assert(original.enabled==0 && original.target==333);
    assert(!oc_settings_read(path,&original) && !memcmp(&original,&c,sizeof(c)));
    c.target=999;assert(oc_settings_write(path,&c)<0);
    assert(!oc_settings_read(path,&c) && c.target==433);
    put("bad.ini","enabled=1\nunknown=3\n");original=c;
    assert(oc_settings_read("bad.ini",&c)<0 && !memcmp(&c,&original,sizeof(c)));
    assert(oc_settings_write("absent/StreamerOC.ini",&c)<0);
    assert(!remove("StreamerOC.ini.bak"));
    assert(!mkdir("StreamerOC.ini.bak",0700));
    put("StreamerOC.ini.bak/keep","user data");
    c.target=222;assert(oc_settings_write(path,&c)<0);
    assert(!oc_settings_read(path,&c) && c.target==433);
    return 0;
}
