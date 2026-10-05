/* SPDX-License-Identifier: GPL-2.0-or-later
 * Small nxdk enumeration adapter for the unchanged shared preset catalog. */
#ifndef XBOX_DIRENT_H
#define XBOX_DIRENT_H
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct dirent {char d_name[260];};
typedef struct {HANDLE handle;WIN32_FIND_DATA data;int first;struct dirent entry;} DIR;
static inline DIR *opendir(const char *path){
    DIR *dir=calloc(1,sizeof(*dir));if(!dir)return NULL;
    char query[600];if(snprintf(query,sizeof(query),"%s\\*",path)>=(int)sizeof(query)){free(dir);return NULL;}
    for(char *p=query;*p;p++)if(*p=='/')*p='\\';
    dir->handle=FindFirstFile(query,&dir->data);dir->first=1;
    if(dir->handle==INVALID_HANDLE_VALUE){free(dir);return NULL;}return dir;
}
static inline struct dirent *readdir(DIR *dir){
    if(!dir || (!dir->first&&!FindNextFile(dir->handle,&dir->data)))return NULL;
    dir->first=0;snprintf(dir->entry.d_name,sizeof(dir->entry.d_name),"%s",dir->data.cFileName);return &dir->entry;
}
static inline int closedir(DIR *dir){if(dir){FindClose(dir->handle);free(dir);}return 0;}
#endif
