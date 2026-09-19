/* Managed local FLV input; shares the streaming decoder and PTS clock. */
#include "offline_filename.h"
static char offline_movie[512], offline_directory[384];
static unsigned long long offline_movie_size;
static int offline_active, offline_seek_ms, offline_profile_tv;
static int offline_music;
static volatile int offline_music_eof;
static unsigned int offline_seek_offset, offline_sprite_base;
static SceUID offline_reader_fd = -1;
static unsigned int offline_u32(const unsigned char *p) {
    return p[0] | ((unsigned int)p[1]<<8) | ((unsigned int)p[2]<<16) | ((unsigned int)p[3]<<24);
}
static int offline_read_file(const char *leaf,void *data,int capacity,unsigned int offset) {
    char path[512];snprintf(path,sizeof(path),"%s/%s",offline_directory,leaf);
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);
    if(fd<0)return fd;
    if(offset && sceIoLseek(fd,(SceOff)offset,PSP_SEEK_SET)<0){sceIoClose(fd);return -1;}
    int total=0,n;
    while(total<capacity && (n=sceIoRead(fd,(char *)data+total,capacity-total))>0)total+=n;
    sceIoClose(fd);return total;
}
static int offline_subtitle_json(void) {
    unsigned char header[8];
    if(offline_read_file("subtitles.ovl",header,8,0)!=8 || memcmp(header,"OVL1",4))return -1;
    unsigned int size=offline_u32(header+4);
    if(size>=RESPONSE_SIZE)return -1;
    if(offline_read_file("subtitles.ovl",response,size,8)!=(int)size)return -1;
    response[size]=0;offline_sprite_base=8+size;return size;
}
static int offline_bitmap(int index,unsigned char *out,int capacity) {
    unsigned int offset=offline_sprite_base;
    int i,size;
    for(i=0;i<index;i++)offset+=1024+bitmap_cues[i].width*bitmap_cues[i].height;
    size=1024+bitmap_cues[index].width*bitmap_cues[index].height;
    if(size<1024 || size>capacity)return -1;
    return offline_read_file("subtitles.ovl",out,size,offset);
}
static void offline_prepare_seek(int seconds) {
    char path[512];unsigned char pair[8];
    offline_seek_ms=0;offline_seek_offset=0;
    if(seconds<=0)return;
    snprintf(path,sizeof(path),"%s/seek.idx",offline_directory);
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);
    if(fd<0)return;
    while(sceIoRead(fd,pair,8)==8) {
        unsigned int pts=offline_u32(pair);
        if(pts>(unsigned int)seconds*1000)break;
        offline_seek_ms=pts;offline_seek_offset=offline_u32(pair+4);
    }
    sceIoClose(fd);
}
