/* Exercises the actual Xbox adapter with command capture, NOT GPU emulation. */
#include <assert.h>
#include <math.h>
#include "../xbox-client/visual_gpu.c"
int main(void){
    assert(sceGuInit()==0);assert(!xv_failed());
    assert(registers[NV097_SET_TRANSFORM_EXECUTION_MODE/4]==6);
    assert(registers[NV097_SET_VERTEX_DATA_ARRAY_FORMAT/4]==0x42);
    assert(registers[(NV097_SET_VERTEX_DATA_ARRAY_FORMAT+9*4)/4]==0x42);
    assert(sceGuStart(0,NULL)==0);
    sceGuDisable(GU_TEXTURE_2D);
    MdVertex triangle[3]={{0,0,0xff0000ff,10,20,0},{0,0,0xff00ff00,30,40,0},{0,0,0xffff0000,50,60,0}};
    sceGuDrawArray(GU_TRIANGLES,GU_TRANSFORM_2D|GU_TEXTURE_32BITF,3,NULL,triangle);
    assert(vertices==3);assert(last_vertex[0]==50&&last_vertex[1]==60&&last_vertex[3]==1);
    assert(last_vertex[4]==0&&last_vertex[5]==0&&last_vertex[6]==1&&last_vertex[7]==1&&last_vertex[11]==1);
    /* Rectangles become complete quads; no PSP sprite topology on NV2A. */
    sceGuDrawArray(GU_SPRITES,GU_TRANSFORM_2D|GU_TEXTURE_32BITF,2,NULL,triangle);
    assert(vertices==7);
    unsigned rgba[4]={0xff0000ff,0xff00ff00,0xffff0000,0xffffffff};
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexImage(0,2,2,2,rgba);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    assert(bind_texture());Texture *t=upload();assert(t&&((unsigned*)t->pixels)[0]==0xffff0000&&((unsigned*)t->pixels)[2]==0xff0000ff);
    Vertex uv=convert((MdVertex){2,2,0xffffffff,1,1,0},GU_TRANSFORM_2D);assert(uv.uv[0]==1&&uv.uv[1]==1&&uv.uv[3]==1);
    /* Correct perspective projection and non-reversed depth. */
    ScePspFMatrix4 projection={.x={1,0,0,0},.y={0,1,0,0},.z={0,0,-1.006689f,-1},.w={0,0,-.200669f,0}};
    sceGuSetMatrix(GU_PROJECTION,&projection);sceGuViewport(0,0,512,256);
    Vertex middle=convert((MdVertex){.color=0xffffffff,.z=-2},GU_TRANSFORM_3D);
    assert(middle.pos[0]==256&&middle.pos[1]==128&&middle.pos[3]==2);
    assert(middle.pos[2]>0&&middle.pos[2]<16777215);
    /* GPU target data must not get the CPU texture red/blue swap. */
    unsigned *feedback=(unsigned*)(xv_ram+768*480*4);feedback[0]=0xff123456;
    sceGuTexImage(0,2,2,2,feedback);assert(bind_texture());t=upload();assert(((unsigned*)t->pixels)[0]==0xff123456);
    /* Long lists wrap before the physical tail, preserving complete packets. */
    sceGuDisable(GU_TEXTURE_2D);unsigned before=wraps;
    for(int i=0;i<8000;i++)sceGuDrawArray(GU_TRIANGLES,GU_TRANSFORM_2D|GU_TEXTURE_32BITF,3,NULL,triangle);
    assert(wraps>before&&packet_max<=128&&!xv_failed());
    sceGuTerm();assert(test_allocations==0&&xv_ram==NULL);
    assert(sceGuInit()==0);sceGuTerm();assert(test_allocations==0);
    puts("Xbox visual adapter: packet bounds, projection, colors, UVs, wrap and teardown passed");
}
