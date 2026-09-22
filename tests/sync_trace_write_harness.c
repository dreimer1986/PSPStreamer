#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int SceUID;
enum {PSP_O_WRONLY=1,PSP_O_CREAT=2,PSP_O_TRUNC=4};
static int debug_enabled=1,audio_blocks_published,audio_played_blocks;
static unsigned int audio_current_timestamp_ms;
static int opens,closes,writes,length,chunk_limit,fail_after;
static char output[1024*1024];
static unsigned long long sceKernelGetSystemTimeWide(void){return 123456;}
static int sceAudioGetChannelRestLen(int channel){(void)channel;return 0;}
static int sceIoOpen(const char *path,int flags,int mode){(void)path;(void)flags;(void)mode;opens++;return 1;}
static int sceIoClose(SceUID fd){assert(fd==1);closes++;return 0;}
static int sceIoWrite(SceUID fd,const void *data,int size){
    assert(fd==1);writes++;
    if(fail_after && writes>=fail_after)return -1;
    if(chunk_limit && size>chunk_limit)size=chunk_limit;
    assert(length+size<(int)sizeof(output));memcpy(output+length,data,size);length+=size;return size;
}
#include "sync_trace.h"
int main(void){
    sync_trace_reset();sync_trace_record(0,1,0);
    sync_trace_count=SYNC_TRACE_CAPACITY+4;
    for(unsigned int i=0;i<sync_trace_count;i++)sync_trace[i%SYNC_TRACE_CAPACITY]=(SyncTraceRow){.elapsed_ms=i};
    sync_trace_save(0,0,0);assert(opens==1 && closes==1 && writes<100);
    int lines=0;for(int i=0;i<length;i++)lines+=output[i]=='\n';
    assert(lines==SYNC_TRACE_CAPACITY+2);
    assert(strstr(output,"\n4,0,0,0,0,0,0,0,0,0,0,0\n"));
    assert(strstr(output,"\n4099,0,0,0,0,0,0,0,0,0,0,0\n"));
    printf("sync CSV: %d rows, %d batched writes\n",SYNC_TRACE_CAPACITY,writes);
    static char expected[sizeof(output)];memcpy(expected,output,length);int expected_length=length;
    length=writes=0;chunk_limit=17;memset(output,0,sizeof(output));
    sync_trace_save(0,0,0);assert(length==expected_length && !memcmp(output,expected,length));
    fail_after=2;writes=length=0;sync_trace_save(0,0,0);assert(writes==2 && closes==3);
    debug_enabled=0;sync_trace_save(0,0,0);assert(opens==3);
}
