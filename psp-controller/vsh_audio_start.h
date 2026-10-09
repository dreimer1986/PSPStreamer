/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
/* Fast loading is only a hint. The bridge still applies context/title policy
 * and waits for an attached, usable USB transport before opening PCM. */
static int consolizer_vsh_audio_requested(const char *text) {
    int enabled=1,vsh=0,mirror=0;
    while(*text) {
        const char *end=strchr(text,'\n');if(!end)end=text+strlen(text);
        while(text<end && (*text==' ' || *text=='\t'))text++;
        const char *tail=end;
        while(tail>text && (tail[-1]=='\r' || tail[-1]==' ' || tail[-1]=='\t'))tail--;
        size_t n=(size_t)(tail-text);
        if(n==9 && !strncmp(text,"enabled=",8))enabled=text[8]=='1';
        else if(n==5 && !strncmp(text,"vsh=",4))vsh=text[4]=='1';
        else if(n==14 && !strncmp(text,"audio_mirror=",13))mirror=text[13]=='1';
        text=*end?end+1:end;
    }
    return enabled && vsh && mirror;
}
