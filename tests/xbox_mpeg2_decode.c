/* Exercise the XBE's decoder adapter and its EOF drain, with exact PTS tags. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../xbox-client/mpeg_video.h"
static int64_t timestamps[10000];static unsigned seen;
static int drain(XboxVideo *v){
    int result;
    while((result=xbox_video_step(v))>0){
        if(seen>=v->submitted||v->pts!=timestamps[seen])return -2;
        seen++;
    }return result;
}
int main(int argc,char **argv){
    if(argc!=2)return 1;FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    XboxVideo v;if(!xbox_video_init(&v))return 3;
    unsigned char header[16];int ended=0;
    while(fread(header,1,16,f)==16){
        unsigned size;int64_t pts;memcpy(&size,header+4,4);memcpy(&pts,header+8,8);
        if(header[0]=='E'){ended=1;break;}
        if(size>1024*1024||v.submitted>=10000)return 4;
        unsigned char *data=malloc(size);if(fread(data,1,size,f)!=size)return 5;
        if(header[0]=='V'){timestamps[v.submitted]=pts;xbox_video_feed(&v,data,size,pts);if(drain(&v)<0)return 6;}
        free(data);
    }
    if(!ended)return 7;
    xbox_video_end(&v);if(drain(&v)<0||v.displayed!=v.submitted)return 8;
    printf("%u\n",v.displayed);xbox_video_close(&v);fclose(f);return 0;
}
