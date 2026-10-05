/* SPDX-License-Identifier: GPL-2.0-or-later
 * File-kind query required by the portable preset browser. */
#ifndef XBOX_PRESET_STAT_H
#define XBOX_PRESET_STAT_H
#include <windows.h>
#include <string.h>
#define S_ISREG(mode) ((mode)==1)
#define S_ISDIR(mode) ((mode)==2)
struct stat {unsigned st_mode;};
static inline int stat(const char *path,struct stat *out){
    char local[600];size_t n=strlen(path);if(n>=sizeof(local))return -1;
    memcpy(local,path,n+1);for(char *p=local;*p;p++)if(*p=='/')*p='\\';
    DWORD attrs=GetFileAttributes(local);if(attrs==INVALID_FILE_ATTRIBUTES)return -1;
    out->st_mode=attrs&FILE_ATTRIBUTE_DIRECTORY?2:1;return 0;
}
#endif
