/* SPDX-License-Identifier: MIT */
#ifndef STREAMER_OC_CONFIG_PARSE_H
#define STREAMER_OC_CONFIG_PARSE_H
#include <string.h>
typedef struct { int enabled, target, enforce, report, overlay, app_control, enforce_unlimited; } OcConfig;
/* No kernel-global strtok state; validate everything before enabling writes.
 * Accept UTF-8 BOM, LF/CRLF, whitespace and whole-line/inline comments. */
static int oc_config_parse(char *text, int length, OcConfig *out, int *keys, int *error_line)
{
    OcConfig draft={0,333,0,1,1,1,0};
    char *cursor=text, *limit=text+length;
    int line_number=0;
    *keys=0; *error_line=0;
    if(length>=3 && (unsigned char)text[0]==0xef &&
       (unsigned char)text[1]==0xbb && (unsigned char)text[2]==0xbf)cursor+=3;
    while(cursor<limit) {
        char *line=cursor, *end, *equal, *key_end, *value;
        int number=0, digits=0;
        ++line_number;
        while(cursor<limit && *cursor!='\n' && *cursor!='\r') {
            if(!*cursor)goto invalid;
            cursor++;
        }
        end=cursor;
        if(cursor<limit) {
            char delimiter=*cursor;
            *cursor++=0;
            if(delimiter=='\r' && cursor<limit && *cursor=='\n')cursor++;
        }
        while(line<end && (*line==' ' || *line=='\t'))line++;
        if(line==end || *line=='#' || *line==';')continue;
        equal=line;
        while(equal<end && *equal!='=')equal++;
        if(equal==end)goto invalid;
        key_end=equal;
        while(key_end>line && (key_end[-1]==' ' || key_end[-1]=='\t'))key_end--;
        *key_end=0;
        value=equal+1;
        while(value<end && (*value==' ' || *value=='\t'))value++;
        while(value<end && *value>='0' && *value<='9') {
            number=number*10+(*value++-'0');
            if(number>471)goto invalid;
            digits++;
        }
        if(!digits)goto invalid;
        while(value<end && (*value==' ' || *value=='\t'))value++;
        if(value<end && *value!='#' && *value!=';')goto invalid;
        if(!strcmp(line,"target_mhz")) {
            if(number<66)goto invalid;
            draft.target=number;
        } else {
            if(number>1)goto invalid;
            if(!strcmp(line,"enabled"))draft.enabled=number;
            else if(!strcmp(line,"enforce"))draft.enforce=number;
            else if(!strcmp(line,"report"))draft.report=number;
            else if(!strcmp(line,"overlay"))draft.overlay=number;
            else if(!strcmp(line,"app_control"))draft.app_control=number;
            else if(!strcmp(line,"enforce_unlimited"))draft.enforce_unlimited=number;
            else goto invalid;
        }
        ++*keys;
    }
    if(!*keys)goto invalid;
    *out=draft;
    return 0;
invalid:
    *error_line=line_number;
    return -1;
}
#endif
