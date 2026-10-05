#ifndef XBOX_VISUAL_TEST_PBKIT_H
#define XBOX_VISUAL_TEST_PBKIT_H
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <nv_regs.h>
static uint32_t commands[131072],*put=commands;
static uint32_t registers[2048];static unsigned wraps,vertices,packet_max;
static float last_vertex[12];
static int pb_busy(void){return 0;}
static void pb_reset(void){put=commands;wraps++;}
static uint32_t *pb_begin(void){return put;}
static void pb_push(uint32_t *p,unsigned method,unsigned count){*p=method|(count<<18);}
static uint32_t *pb_push1(uint32_t *p,unsigned method,unsigned value){pb_push(p,method,1);p[1]=value;return p+2;}
static void pb_end(uint32_t *end){
    assert(end>=put&&end<=commands+131072);unsigned words=end-put;assert(words<=1152);if(words>packet_max)packet_max=words;
    while(put<end){unsigned header=*put++,count=(header>>18)&2047,method=header&0x1fff;assert(put+count<=end);
        if(method==NV097_INLINE_ARRAY){assert(count<=120&&count%12==0);vertices+=count/12;memcpy(last_vertex,put+count-12,sizeof(last_vertex));}
        else for(unsigned i=0;i<count;i++)registers[((method+((header&0x40000000)?0:4*i))&0x1fff)/4]=put[i];
        put+=count;
    }
}
static void pb_size(unsigned size){assert(size==512*1024);}
static int pb_init(void){return 0;}
static void pb_kill(void){}
static void pb_show_debug_screen(void){}
#endif
