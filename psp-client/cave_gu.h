/* Native cached isosurface geometry; shares the music renderer's GU owner. */
#include "cave_clip.h"
static CaveScene *cave_scene;
static uint32_t cave_pixels[CAVE_TEXTURE*CAVE_TEXTURE] __attribute__((aligned(64)));
static int cave_texture_ready;
typedef struct {float sx,sy,u,v,detail_v;unsigned gain;} CaveTextureMotion;
/* Reuse each clipped/cached batch for both fixed-function texture stages.
 * No second mesh, texture allocation, field evaluation or audio work. */
static void cave_draw_batch(int count,const MdVertex *vertices,const CaveTextureMotion *t) {
    sceGuDisable(GU_BLEND);sceGuDepthMask(0);
    sceGuTexScale(t->sx,t->sy);sceGuTexOffset(t->u,t->v);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,NULL,vertices);
    /* A dim, counter-moving detail layer; equal-depth test, no depth writes.
     * Fog remains active, so the extra layer also fades into the distance. */
    sceGuDepthMask(1);sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,t->gain*0x010101,0xffffff);
    sceGuTexScale(2,2);sceGuTexOffset(-2*t->u,t->detail_v);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,NULL,vertices);
    sceGuDisable(GU_BLEND);sceGuDepthMask(0);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);
}
static void cave_clear_target(void) {
    /* sceGuClear uses gu_draw_buffer.width/height (480x272 after GU init),
     * NOT the offscreen viewport set by sceGuDrawBufferList. Clear the full
     * 512x256 color/Z target explicitly, without changing display ownership.
     * GE CLEAR_MODE = 0xd3; normal rendering is restored immediately. */
    MdPlainVertex *v=sceGuGetMemory(2*sizeof(*v));
    v[0]=(MdPlainVertex){0xff000000,0,0,0};
    v[1]=(MdPlainVertex){0xff000000,MD_WIDTH,MD_HEIGHT,0};
    sceGuSendCommandi(0xd3,((GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT)<<8)|1);
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,v);
    sceGuSendCommandi(0xd3,0);
}
static void cave_draw(int width,int height) {
    if(!cave_texture_ready) {
        cave_texture(cave_pixels);cave_texture_ready=1;
        sceKernelDcacheWritebackRange(cave_pixels,sizeof(cave_pixels));
    }
    md_target(MD_TEXTURE_BASE,MD_WIDTH,MD_WIDTH,MD_HEIGHT);
    sceGuDepthBuffer((void *)(uintptr_t)(MD_TEXTURE_BASE+MD_TEXTURE_BYTES),MD_WIDTH);
    sceGuDepthRange(65535,0);sceGuDepthMask(0);sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);
    sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_FOG);sceGuFog(4,14,0xff000000);
    cave_clear_target();
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexImage(0,CAVE_TEXTURE,CAVE_TEXTURE,CAVE_TEXTURE,cave_pixels);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexFlush();
    ScePspFMatrix4 identity={.x={1,0,0,0},.y={0,1,0,0},.z={0,0,1,0},.w={0,0,0,1}};
    ScePspFMatrix4 view;
    float view_values[16];cave_view(cave_scene,cave_scene->motion.travel,view_values);
    memcpy(&view,view_values,sizeof(view));
    ScePspFMatrix4 projection={.x={1.25f*height/width,0,0,0},.y={0,1.25f,0,0},
        .z={0,0,-1.006689f,-1},.w={0,0,-.200669f,0}};
    sceGuSetMatrix(GU_PROJECTION,&projection);sceGuSetMatrix(GU_VIEW,&view);sceGuSetMatrix(GU_MODEL,&identity);
    int first=(int)floorf(cave_scene->motion.travel)-CAVE_HISTORY;
    CaveClip clip;cave_clip_init(&clip,view_values,projection.x.x,projection.y.y);
    float phase=cave_scene->motion.phase;
    CaveTextureMotion texture={1+.035f*sinf(phase*2),1+.035f*cosf(phase*2),
        phase/6.283185307f,.06f*sinf(phase*3),.12f*sinf(phase*2),
        12+(unsigned)(16*cave_scene->motion.bass+8*cave_scene->motion.pulse)};
    unsigned budget=0;
    for(int i=0;i<CAVE_SLICES;i++) {
        CaveSlice *slice=&cave_scene->slices[i];
        if(slice->index<first || !slice->count)continue;
        int begin=0;
        for(int at=0;at<=slice->count;at+=3) {
            unsigned a=0,b=0,c=0;
            if(at<slice->count) {
                a=cave_clip_mask(&clip,&slice->vertices[at]);
                b=cave_clip_mask(&clip,&slice->vertices[at+1]);
                c=cave_clip_mask(&clip,&slice->vertices[at+2]);
                if(!(a|b|c))continue; /* Keep cached, contiguous safe geometry. */
            }
            if(at>begin) {
                if(budget+384>MD_LIST_BYTES-65536)return;
                budget+=384;
                cave_draw_batch(at-begin,slice->vertices+begin,&texture);
            }
            begin=at+3;
            if(at==slice->count || (a&b&c))continue;
            MdVertex clipped[CAVE_CLIP_VERTICES];
            int count=cave_clip_triangle(&clip,slice->vertices+at,clipped);
            if(count<=0)continue;
            unsigned bytes=count*sizeof(MdVertex);
            /* Reserve command/presentation headroom even at pathological
             * geometry density. Scratch belongs to this GU list until sync. */
            if(budget+bytes+464>MD_LIST_BYTES-65536)return;
            budget+=bytes+464;
            unsigned char *memory=sceGuGetMemory(bytes+63);
            MdVertex *vertices=(MdVertex *)(((uintptr_t)memory+63)&~(uintptr_t)63);
            memcpy(vertices,clipped,bytes);
            cave_draw_batch(count,vertices,&texture);
        }
    }
}
