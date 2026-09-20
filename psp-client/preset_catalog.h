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
static inline int preset_relative_valid(const char *name) {
    if(!*name || strlen(name)>=256 || strchr(name,':') || strchr(name,'\\') || strchr(name,'\n') || strchr(name,'\r'))return 0;
    const char *p=name;
    while(*p) {
        const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
        if(!n || (n==1 && *p=='.') || (n==2 && p[0]=='.' && p[1]=='.'))return 0;
        if(!end)break;
        p=end+1;if(!*p)return 0;
    }
    return 1;
}
static inline int preset_path_valid(const char *name) {
    size_t n=strlen(name);
    return n>5 && preset_relative_valid(name) && !strcasecmp(name+n-5,".milk");
}
static int preset_name_valid(const char *name) {
    size_t n=strlen(name);
    return n>5 && n<256 && !strcasecmp(name+n-5,".milk") && !strchr(name,':') &&
        !strchr(name,'/') && !strchr(name,'\\') && !strchr(name,'\n') && !strchr(name,'\r');
}
static int preset_name_compare(const void *a,const void *b) { return strcasecmp(a,b); }
static inline int preset_catalog_load(PresetCatalog *catalog,const char *directory) {
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
/* One directory per page set; trailing '/' marks folders, '..' goes up.
 * Existing automatic playlists keep their flat file-only catalog. */
static inline int preset_catalog_browse(PresetCatalog *catalog,const char *directory,int parent) {
    DIR *dir=opendir(directory);struct dirent *entry;
    catalog->count=catalog->truncated=0;
    if(parent)strcpy(catalog->names[catalog->count++],"..");
    if(!dir)return 0;
    while((entry=readdir(dir))) {
        char path[512];struct stat info;
        if(!preset_relative_valid(entry->d_name) || strchr(entry->d_name,'/'))continue;
        if(snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name)>=(int)sizeof(path) || stat(path,&info))continue;
        int folder=S_ISDIR(info.st_mode);
        if((!folder && (!S_ISREG(info.st_mode)||!preset_name_valid(entry->d_name))) || strlen(entry->d_name)+folder>=256)continue;
        if(catalog->count==PRESET_CATALOG_LIMIT){catalog->truncated=1;continue;}
        char *name=catalog->names[catalog->count++];strcpy(name,entry->d_name);if(folder)strcat(name,"/");
    }
    closedir(dir);
    qsort(catalog->names+parent,catalog->count-parent,sizeof(catalog->names[0]),preset_name_compare);
    return 1;
}
#endif
