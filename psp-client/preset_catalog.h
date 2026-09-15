#ifndef PSP_STREAMER_PRESET_CATALOG_H
#define PSP_STREAMER_PRESET_CATALOG_H
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
enum { PRESET_CATALOG_LIMIT=128 };
typedef struct { int count, truncated; char names[PRESET_CATALOG_LIMIT][256]; } PresetCatalog;
static int preset_name_valid(const char *name) {
    size_t n=strlen(name);
    return n>5 && n<256 && !strcasecmp(name+n-5,".milk") &&
        !strchr(name,'/') && !strchr(name,'\\') && !strchr(name,'\n') && !strchr(name,'\r');
}
static int preset_name_compare(const void *a,const void *b) { return strcasecmp(a,b); }
static int preset_catalog_load(PresetCatalog *catalog,const char *directory) {
    DIR *dir=opendir(directory); struct dirent *entry;
    catalog->count=catalog->truncated=0;
    if(!dir) return 0;
    while((entry=readdir(dir))) {
        char path[512]; struct stat info;
        if(!preset_name_valid(entry->d_name)) continue;
        if(snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name)>=(int)sizeof(path)) continue;
        if(stat(path,&info) || !S_ISREG(info.st_mode)) continue;
        if(catalog->count==PRESET_CATALOG_LIMIT) { catalog->truncated=1; continue; }
        strcpy(catalog->names[catalog->count++],entry->d_name);
    }
    closedir(dir);
    qsort(catalog->names,catalog->count,sizeof(catalog->names[0]),preset_name_compare);
    return 1;
}
#endif
