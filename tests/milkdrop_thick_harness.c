#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "milkdrop_warp.h"
enum {GU_COLOR_8888=2,GU_VERTEX_32BITF=4,GU_TRANSFORM_2D=8,GU_POINTS=124};
static unsigned char arena[3*2048*16];
static const void *draw_data[6];
static int draw_count[6],draws,allocations;
static void *sceGuGetMemory(int bytes) {
    assert(bytes>0 && (size_t)bytes<=sizeof(arena));allocations++;return arena;
}
static void sceGuDrawArray(int primitive,int format,int count,const void *indices,const void *vertices) {
    assert((primitive==123 || primitive==GU_POINTS) && format==14 && !indices && draws<6);
    draw_data[draws]=vertices;draw_count[draws++]=count;
}
/* THICK_HELPER */
int main(void) {
    MdVertex vertices[2048],before[2048];
    for(int i=0;i<2048;i++)vertices[i]=(MdVertex){.u=99,.v=77,.color=0x12345678U+i,
        .x=i*.25f-10,.y=i*.125f,.z=.5f};
    memcpy(before,vertices,sizeof(before));
    for(int count=1;count<=2048;count=count==1?127:count==127?2048:2049)
    for(int divided=0;divided<2;divided++) {
        int split=divided?count/2:0;
        draws=allocations=0;
        md_thick_wave(123,vertices,count,split);
        assert(allocations==1 && draws==(split?6:3));
        assert(!memcmp(before,vertices,sizeof(before)));
        /* Inspect only after all draws have been queued: no reused/mutated
         * backing data may erase an earlier offset or split segment. */
        for(int d=0;d<draws;d++) {
            int pass=split?d/2:d,start=split && d%2?split:0;
            int n=split?(d%2?count-split:split):count;
            assert(draw_count[d]==n);
            const MdPlainVertex *p=draw_data[d];
            for(int i=0;i<n;i++) {
                const MdVertex *v=&vertices[start+i];
                assert(p[i].color==v->color && p[i].z==v->z);
                assert(p[i].x==v->x+(pass<2?1:0));
                assert(p[i].y==v->y-(pass>0?1:0));
            }
        }
        draws=allocations=0;
        md_thick_wave(GU_POINTS,vertices,count,split);
        assert(allocations==1 && draws==1 && draw_count[0]==3*count);
        const MdPlainVertex *points=draw_data[0];
        for(int pass=0;pass<3;pass++)for(int i=0;i<count;i++) {
            const MdPlainVertex *p=points+pass*count+i;
            assert(p->color==vertices[i].color && p->z==vertices[i].z);
            assert(p->x==vertices[i].x+(pass<2?1:0));
            assert(p->y==vertices[i].y-(pass>0?1:0));
        }
    }
    return 0;
}
