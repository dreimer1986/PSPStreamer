/* PC-created FAT long names are not necessarily addressable as UTF-8 by the
 * homebrew file API. Use the SDK's FAT short-name result, not a guessed ~1
 * alias or a global filesystem-encoding change. Each managed job has one FLV.
 * PSPSDK: SceIoFatDirentPrivate in pspiofilemgr_dirent.h. */
static unsigned long long offline_manifest_movie_size(const char *meta) {
    const char *files=strstr(meta,"\"files\":["), *size, *end;
    if(!files || !(end=strchr(files,'}')) || !(size=strstr(files,"\"size\":")) || size>end)return 0;
    return strtoull(size+7,NULL,10);
}
static int offline_short_flv(const char *name,size_t capacity) {
    size_t n=0;
    while(n<capacity && name[n]) {
        unsigned char c=name[n++];
        if(c<33 || c>=127 || strchr("/\\:<>\"|?*",c))return 0;
    }
    return n>4 && n<capacity && !strcasecmp(name+n-4,".flv");
}
/* On success path contains the actual addressable filename. Only fall back
 * after ENOENT, and reject ambiguous, missing-size and directory matches. */
static SceUID offline_open_movie(char *path,size_t capacity,unsigned long long expected) {
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);
    if(fd>=0 || fd!=(int)0x80010002 || !expected)return fd;
    const char *slash=strrchr(path,'/');
    char folder[512],candidate[512];
    if(!slash || (size_t)(slash-path)>=sizeof(folder))return fd;
    memcpy(folder,path,slash-path);folder[slash-path]=0;
    SceUID dir=sceIoDopen(folder);
    if(dir<0)return fd;
    SceIoDirent entry;
    SceIoFatDirentPrivate extra;
    int count=0,result;
    for(;;) {
        memset(&entry,0,sizeof(entry));memset(&extra,0,sizeof(extra));
        extra.size=sizeof(extra);entry.d_private=&extra;
        result=sceIoDread(dir,&entry);
        if(result<=0)break;
        if(FIO_S_ISDIR(entry.d_stat.st_mode) || (unsigned long long)entry.d_stat.st_size!=expected)continue;
        /* Older compiled-SDK ABIs return the 13-byte alias at offset zero;
         * newer ones retain the size word and put it at offset four. */
        const char *alias=extra.size==sizeof(extra)?extra.s_name:(const char *)&extra;
        if(!offline_short_flv(alias,13))continue;
        int n=snprintf(candidate,sizeof(candidate),"%s/%s",folder,alias);
        if(n<0 || n>=(int)sizeof(candidate) || n>=(int)capacity){count=2;break;}
        if(++count>1)break;
    }
    sceIoDclose(dir);
    if(result<0 || count!=1)return fd;
    SceUID resolved=sceIoOpen(candidate,PSP_O_RDONLY,0);
    if(resolved>=0)strcpy(path,candidate);
    return resolved;
}
