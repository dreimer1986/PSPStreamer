#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "milkdrop_preset.h"
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void __real_free(void *);
static struct {void *p;size_t size;} allocations[256];
static size_t live,peak;
static int calls,fail_at;
static void track(void *old,void *p,size_t size) {
    if(!p)return;
    int slot=-1;
    for(int i=0;i<256;i++)if(allocations[i].p==old && (old || slot<0)) {slot=i;if(old)break;}
    assert(slot>=0);
    live-=allocations[slot].size;live+=size;
    allocations[slot].p=p;allocations[slot].size=size;
    if(live>peak)peak=live;
}
void *__wrap_calloc(size_t n,size_t size) {
    if(++calls==fail_at)return NULL;
    void *p=__real_calloc(n,size);track(NULL,p,n*size);return p;
}
void *__wrap_realloc(void *old,size_t size) {
    if(++calls==fail_at)return NULL;
    void *p=__real_realloc(old,size);track(old,p,size);return p;
}
void __wrap_free(void *p) {
    if(!p)return;
    int found=0;
    for(int i=0;i<256;i++)if(allocations[i].p==p) {
        live-=allocations[i].size;allocations[i].size=0;allocations[i].p=NULL;found=1;break;
    }
    assert(found);__real_free(p);
}
int main(int argc,char **argv) {
    assert(argc==3);
    MdFilePreset preset={0},before;
    MdFileError error;
    assert(md_load_preset(argv[2],&preset,&error)==MD_FILE_OK);
    int import_calls=calls;
    md_free_preset(&preset);assert(!live);
    for(int failure=1;failure<=import_calls;failure++) {
        fail_at=0;
        assert(md_load_preset(argv[1],&preset,&error)==MD_FILE_OK);
        before=preset;size_t previous=live;
        calls=0;fail_at=failure;
        int result=md_load_preset(argv[2],&preset,&error);
        if(result!=MD_FILE_OK) {
            assert(result==MD_FILE_IO);
            assert(!memcmp(&before,&preset,sizeof(preset)) && live==previous);
            MdPreset warp;unsigned color;
            int before_calls=calls;
            assert(md_eval_preset(&preset,0,&warp,&color,&error)==MD_FILE_OK);
            assert(calls==before_calls); /* no playback-time allocation */
        }
        fail_at=0;md_free_preset(&preset);assert(!live);
        md_free_preset(&preset);assert(!live); /* idempotent teardown */
    }
    for(int i=0;i<100;i++) {
        assert(md_load_preset(argv[1+i%2],&preset,&error)==MD_FILE_OK);
    }
    md_free_preset(&preset);assert(!live);
    printf("Preset storage: %d injected allocation failures; peak tracked heap %zu bytes; preset %zu, activation %zu bytes\n",
           import_calls,peak,sizeof(preset),sizeof(MdPresetState));
    return 0;
}
