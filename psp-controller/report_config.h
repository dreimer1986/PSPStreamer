/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
static int consolizer_report_setting(const char *text) {
    int enabled=1;
    while(*text) {
        const char *end=strchr(text,'\n');if(!end)end=text+strlen(text);
        while(text<end && (*text==' ' || *text=='\t'))text++;
        if(end-text>=8 && !strncmp(text,"report=",7)) {
            const char *value=text+7,*tail=value+1;
            while(tail<end && (*tail==' ' || *tail=='\t' || *tail=='\r'))tail++;
            if(tail==end && (*value=='0' || *value=='1'))enabled=*value-'0';
        }
        text=*end?end+1:end;
    }
    return enabled;
}
