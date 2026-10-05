/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "visual_gpu.h"
#include "milkdrop_warp.h"
#include <pbkit/pbkit.h>
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

unsigned char *xv_ram;
int xv_width=720,xv_height=480;
static int running,failed,enabled[10],target_stride=768,target_w=720,target_h=480;
static int tex_format,tex_swizzled,tex_w,tex_h,tex_stride,tex_repeat,tex_linear=1,tex_alpha=1;
static const void *tex_source;
static unsigned char *scratch;static size_t scratch_used;
static unsigned char *depth;
static int clearing,depth_mask=1;
static float matrices[3][16],fog_start,fog_end;
static unsigned fog_color;
static float tex_scale[2]={1,1},tex_offset[2];
static char failure[96];
static const void *native[8];static int native_next;
static void *target;
typedef struct {const void *source;void *pixels;int w,h,format,swizzled,dirty;unsigned age;} Texture;
static Texture textures[20];static unsigned texture_bytes,age;
/* Reserve a complete pbkit packet before wrapping. Command-stream jumps keep
 * the active NV097 primitive and triangle-strip continuity intact. */
static size_t push_used;static uint32_t *push_start;
static uint32_t *visual_push_begin(void){
    if(push_used+129>=512*1024/4){pb_reset();push_used=0;}
    push_start=pb_begin();return push_start;
}
static void visual_push_end(uint32_t *end){push_used+=end-push_start;pb_end(end);}
#define pb_begin visual_push_begin
#define pb_end visual_push_end
#define SCRATCH_BYTES (1572864+65536)
#define TEXTURE_BUDGET (3*1024*1024)
#define PHYSICAL(p) ((uint32_t)(uintptr_t)(p)&0x03ffffff)
static void fail(const char *s){if(!failed)snprintf(failure,sizeof(failure),"%s",s);failed=1;}
int xv_failed(void){return failed;}
const char *xv_error(void){return failure;}
static void reg(unsigned method,unsigned value){if(!running||failed)return;uint32_t *p=pb_begin();p=pb_push1(p,method,value);pb_end(p);}
static int wait_gpu(void){
    if(!running)return 1;unsigned start=GetTickCount();
    while(pb_busy()){if(GetTickCount()-start>500){fail("NV2A command timeout");return 0;}Sleep(0);}
    return !failed;
}
unsigned long long sceKernelGetSystemTimeWide(void){return (unsigned long long)GetTickCount()*1000;}
static void fragment(int texture){
    uint32_t *p=pb_begin();
    if(texture&&tex_alpha){
#include "visual_texture.inl"
    }else if(texture){
#include "visual_rgb.inl"
    }else{
#include "visual_plain.inl"
    }
    pb_end(p);
    if(!texture)reg(NV097_SET_SHADER_STAGE_PROGRAM,0);
}
int sceGuInit(void){
    if(running)return 0;
    failed=0;failure[0]=0;
    scratch=malloc(SCRATCH_BYTES);
    xv_ram=MmAllocateContiguousMemoryEx(4*1024*1024,0,0x03ffb000,64,PAGE_READWRITE|PAGE_WRITECOMBINE);
    depth=MmAllocateContiguousMemoryEx(512*512*4,0,0x03ffb000,64,PAGE_READWRITE|PAGE_WRITECOMBINE);
    if(!scratch||!xv_ram||!depth){sceGuTerm();fail("Not enough visualization memory");return -1;}
    memset(xv_ram,0,4*1024*1024);
    pb_size(512*1024);
    int result=pb_init();if(result){sceGuTerm();fail("pbkit initialization failed");return -1;}
    running=1;pb_show_debug_screen();pb_reset();push_used=0;
    static const uint32_t program[]={
#include "visual_vertex.inl"
    };
    reg(NV097_SET_TRANSFORM_PROGRAM_START,0);
    reg(NV097_SET_TRANSFORM_EXECUTION_MODE,0x06);
    reg(NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN,0);
    reg(NV097_SET_TRANSFORM_PROGRAM_LOAD,0);
    for(unsigned i=0;i<sizeof(program)/sizeof(program[0]);i+=4){uint32_t *p=pb_begin();pb_push(p++,NV097_SET_TRANSFORM_PROGRAM,4);memcpy(p,program+i,16);pb_end(p+4);}
    for(int i=0;i<16;i++)reg(NV097_SET_VERTEX_DATA_ARRAY_FORMAT+4*i,2);
    reg(NV097_SET_VERTEX_DATA_ARRAY_FORMAT,2|(4<<4));
    reg(NV097_SET_VERTEX_DATA_ARRAY_FORMAT+3*4,2|(4<<4));
    reg(NV097_SET_VERTEX_DATA_ARRAY_FORMAT+9*4,2|(4<<4));
    for(int i=0;i<4;i++)reg(NV097_SET_TEXTURE_CONTROL0+64*i,0);
    reg(NV097_SET_CULL_FACE_ENABLE,0);reg(NV097_SET_LIGHTING_ENABLE,0);
    reg(NV097_SET_FOG_ENABLE,0);reg(NV097_SET_ALPHA_TEST_ENABLE,0);
    reg(NV097_SET_STENCIL_TEST_ENABLE,0);
    reg(NV097_SET_COLOR_MASK,0x01010101);
    reg(NV097_SET_SHADE_MODEL,0x1d01);
    reg(NV097_SET_DEPTH_FUNC,0x0203); /* LEQUAL, standard NV2A depth. */
    reg(NV097_SET_BLEND_EQUATION,0x8006);
    memset(enabled,0,sizeof(enabled));memset(native,0,sizeof(native));native_next=0;
    for(int m=0;m<3;m++){memset(matrices[m],0,64);for(int i=0;i<4;i++)matrices[m][5*i]=1;}
    sceGuDrawBufferList(GU_PSM_8888,0,768);sceGuScissor(0,0,720,480);
    return failed?-1:0;
}
void sceGuTerm(void){
    if(running){wait_gpu();pb_show_debug_screen();pb_kill();running=0;}
    for(unsigned i=0;i<20;i++){if(textures[i].pixels)MmFreeContiguousMemory(textures[i].pixels);memset(&textures[i],0,sizeof(textures[i]));}
    texture_bytes=0;if(xv_ram)MmFreeContiguousMemory(xv_ram);xv_ram=NULL;
    if(depth)MmFreeContiguousMemory(depth);depth=NULL;free(scratch);scratch=NULL;
}
int sceGuStart(int mode,void *list){(void)mode;(void)list;if(!running||!wait_gpu())return -1;pb_reset();push_used=0;scratch_used=0;return 0;}
void sceGuFinish(void){}
void sceGuSync(int mode,int what){(void)mode;(void)what;wait_gpu();}
void *sceGuGetMemory(size_t bytes){
    bytes=(bytes+15)&~15U;
    if(bytes>SCRATCH_BYTES-scratch_used){fail("Visualization scratch exhausted");return scratch;}
    void *out=scratch+scratch_used;scratch_used+=bytes;return out;
}
void sceGuDrawBufferList(int format,void *offset,int stride){
    (void)format;size_t address=(uintptr_t)offset;if(address>=4*1024*1024){fail("Invalid visual target");return;}
    target=xv_ram+address;target_stride=stride;
    reg(NV097_SET_SURFACE_FORMAT,0x128); /* pitched ARGB8888 + Z24S8 */
    reg(NV097_SET_SURFACE_PITCH,(512*4<<16)|(stride*4));
    reg(NV097_SET_SURFACE_COLOR_OFFSET,PHYSICAL(target));
    reg(NV097_SET_SURFACE_ZETA_OFFSET,PHYSICAL(depth));
}
void sceGuDepthBuffer(void *offset,int stride){(void)offset;(void)stride;}
void sceGuDepthMask(int mask){depth_mask=mask;reg(NV097_SET_DEPTH_MASK,!mask);}
void sceGuDepthFunc(int func){(void)func;reg(NV097_SET_DEPTH_FUNC,0x0203);}
void sceGuDepthRange(int near_value,int far_value){(void)near_value;(void)far_value;}
void sceGuEnable(int cap){if(cap<10)enabled[cap]=1;if(cap==GU_BLEND)reg(NV097_SET_BLEND_ENABLE,1);if(cap==GU_DEPTH_TEST)reg(NV097_SET_DEPTH_TEST_ENABLE,1);}
void sceGuDisable(int cap){if(cap<10)enabled[cap]=0;if(cap==GU_BLEND)reg(NV097_SET_BLEND_ENABLE,0);if(cap==GU_DEPTH_TEST)reg(NV097_SET_DEPTH_TEST_ENABLE,0);}
static unsigned blend(int op,unsigned fixed,int source){
    switch(op){case GU_SRC_ALPHA:return 0x302;case GU_ONE_MINUS_SRC_ALPHA:return 0x303;
    case GU_OTHER_COLOR:return source?0x306:0x300;case GU_ONE_MINUS_OTHER_COLOR:return source?0x307:0x301;
    case GU_FIX:return fixed?1:0;default:fail("Unsupported blend factor");return 1;}
}
void sceGuBlendFunc(int op,int src,int dst,unsigned a,unsigned b){(void)op;reg(NV097_SET_BLEND_FUNC_SFACTOR,blend(src,a,1));reg(NV097_SET_BLEND_FUNC_DFACTOR,blend(dst,b,0));}
void sceGuSetMatrix(int kind,const ScePspFMatrix4 *m){if(kind>=0&&kind<3)memcpy(matrices[kind],m,64);}
void sceGuFog(float start,float end,unsigned color){fog_start=start;fog_end=end;fog_color=color;}
void sceGuOffset(int x,int y){(void)x;(void)y;}
void sceGuViewport(int x,int y,int w,int h){(void)x;(void)y;target_w=w;target_h=h;}
void sceGuScissor(int x,int y,int w,int h){
    reg(NV097_SET_SURFACE_CLIP_HORIZONTAL,(w<<16)|x);reg(NV097_SET_SURFACE_CLIP_VERTICAL,(h<<16)|y);
    reg(NV097_SET_WINDOW_CLIP_TYPE,0);
    reg(NV097_SET_WINDOW_CLIP_HORIZONTAL,((w-1)<<16)|x);reg(NV097_SET_WINDOW_CLIP_VERTICAL,((h-1)<<16)|y);
}
void sceGuShadeModel(int mode){(void)mode;reg(NV097_SET_SHADE_MODEL,0x1d01);}
void sceGuTexMode(int fmt,int levels,int unknown,int swizzled){(void)levels;(void)unknown;tex_format=fmt;tex_swizzled=swizzled;}
void sceGuTexImage(int level,int w,int h,int stride,const void *image){(void)level;tex_w=w;tex_h=h;tex_stride=stride;tex_source=image;}
void sceGuTexFunc(int function,int alpha){(void)function;tex_alpha=alpha;}
void sceGuTexFilter(int min,int mag){(void)mag;tex_linear=min==GU_LINEAR;}
void sceGuTexWrap(int u,int v){(void)v;tex_repeat=u==GU_REPEAT;}
void sceGuTexScale(float u,float v){tex_scale[0]=u;tex_scale[1]=v;}
void sceGuTexOffset(float u,float v){tex_offset[0]=u;tex_offset[1]=v;}
void sceGuTexFlush(void){}
void sceGuTexSync(void){wait_gpu();}
void xv_native_texture(void *ptr){if(!ptr){memset(native,0,sizeof(native));native_next=0;}else native[native_next++%8]=ptr;}
static int is_native(const void *p){if((uintptr_t)p>=(uintptr_t)xv_ram&&(uintptr_t)p<(uintptr_t)xv_ram+4*1024*1024)return 1;for(int i=0;i<8;i++)if(native[i]==p)return 1;return 0;}
static int is_target(const void *p){return xv_ram&&(uintptr_t)p>=(uintptr_t)xv_ram&&(uintptr_t)p<(uintptr_t)xv_ram+4*1024*1024;}
void sceKernelDcacheWritebackRange(const void *ptr,size_t size){
    for(int i=0;i<20;i++)if((uintptr_t)textures[i].source>=(uintptr_t)ptr&&(uintptr_t)textures[i].source<(uintptr_t)ptr+size)textures[i].dirty=1;
}
static unsigned morton(unsigned x,unsigned y,unsigned w,unsigned h){unsigned out=0,bit=1;for(unsigned mask=1;mask<w||mask<h;mask<<=1){if(mask<w){if(x&mask)out|=bit;bit<<=1;}if(mask<h){if(y&mask)out|=bit;bit<<=1;}}return out;}
static Texture *upload(void){
    Texture *t=NULL;
    for(int i=0;i<20;i++)if(textures[i].source==tex_source&&textures[i].w==tex_w&&textures[i].h==tex_h&&textures[i].format==tex_format&&textures[i].swizzled==tex_swizzled){t=textures+i;break;}
    if(!t){
        unsigned bytes=tex_w*tex_h*4;
        if(tex_w<1||tex_h<1||tex_w>1024||tex_h>1024||bytes>TEXTURE_BUDGET){fail("Invalid visual texture");return NULL;}
        if(!wait_gpu())return NULL;
        while(!t||texture_bytes+bytes>TEXTURE_BUDGET){
            Texture *old=NULL;for(int i=0;i<20;i++){if(!textures[i].pixels){if(!t)t=textures+i;continue;}if(!old||textures[i].age<old->age)old=textures+i;}
            if(t&&texture_bytes+bytes<=TEXTURE_BUDGET)break;
            if(!old){fail("Texture cache exhausted");return NULL;}
            texture_bytes-=old->w*old->h*4;MmFreeContiguousMemory(old->pixels);memset(old,0,sizeof(*old));if(!t)t=old;
        }
        t->pixels=MmAllocateContiguousMemoryEx(bytes,0,0x03ffb000,64,PAGE_READWRITE|PAGE_WRITECOMBINE);
        if(!t->pixels){memset(t,0,sizeof(*t));fail("Texture allocation failed");return NULL;}
        t->source=tex_source;t->w=tex_w;t->h=tex_h;t->format=tex_format;t->swizzled=tex_swizzled;t->dirty=1;texture_bytes+=bytes;
    }
    t->age=++age;
    /* Mutable feedback is invalidated by target writes below. Reuse its
     * swizzled sampler for repeated shape passes until that surface changes. */
    if(t->dirty){
        if(!wait_gpu())return NULL;
        int bpp=tex_format==GU_PSM_8888?4:2;
        for(int y=0;y<tex_h;y++)for(int x=0;x<tex_w;x++){
            unsigned at=(y*tex_stride+x)*bpp;
            if(tex_swizzled)at=((y/8)*(tex_stride*bpp/16)+(x*bpp/16))*128+(y%8)*16+x*bpp%16;
            const unsigned char *s=(const unsigned char*)tex_source+at;unsigned c;
            if(bpp==4){memcpy(&c,s,4);if(!is_native(tex_source))c=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);}
            else{unsigned a=s[0]|s[1]<<8;if(tex_format==GU_PSM_5650)c=0xff000000|((a&31)*255/31<<16)|(((a>>5)&63)*255/63<<8)|((a>>11)*255/31);
                else c=((a>>12)*17<<24)|((a&15)*17<<16)|(((a>>4)&15)*17<<8)|(((a>>8)&15)*17);}
            ((unsigned*)t->pixels)[morton(x,y,tex_w,tex_h)]=c;
        }
        __asm__ volatile("sfence":::"memory");t->dirty=0;
    }return t;
}
static int bind_texture(void){
    if(!enabled[GU_TEXTURE_2D]){fragment(0);reg(NV097_SET_TEXTURE_CONTROL0,0);return 1;}
    fragment(1);const void *data;unsigned format;
    if(is_target(tex_source)&&!tex_repeat) {data=tex_source;format=0x1122a;}
    else {Texture *t=upload();if(!t)return 0;data=t->pixels;unsigned u=0,v=0;while((1U<<u)<(unsigned)tex_w)u++;while((1U<<v)<(unsigned)tex_h)v++;format=0x1062a|(u<<20)|(v<<24);}
    reg(NV097_SET_TEXTURE_OFFSET,PHYSICAL(data));reg(NV097_SET_TEXTURE_FORMAT,format);
    reg(NV097_SET_TEXTURE_CONTROL1,tex_stride*4<<16);
    reg(NV097_SET_TEXTURE_IMAGE_RECT,(tex_w<<16)|tex_h);
    reg(NV097_SET_TEXTURE_ADDRESS,tex_repeat?0x010101:0x030303);
    reg(NV097_SET_TEXTURE_CONTROL0,0x4003ffc0);
    reg(NV097_SET_TEXTURE_FILTER,tex_linear?0x02022000:0x01012000);
    return !failed;
}
static void transform(const float *matrix,float p[4]){float q[4];for(int i=0;i<4;i++)q[i]=matrix[i]*p[0]+matrix[4+i]*p[1]+matrix[8+i]*p[2]+matrix[12+i]*p[3];memcpy(p,q,sizeof(q));}
typedef struct {float pos[4],color[4],uv[4];} Vertex;
static Vertex convert(MdVertex v,int format){
    Vertex out={{v.x,v.y,v.z,1},{(v.color&255)/255.f,((v.color>>8)&255)/255.f,((v.color>>16)&255)/255.f,(v.color>>24)/255.f},{v.u,v.v,0,1}};
    if(!(format&GU_TRANSFORM_2D)){
        transform(matrices[GU_MODEL],out.pos);transform(matrices[GU_VIEW],out.pos);
        float distance=-out.pos[2];transform(matrices[GU_PROJECTION],out.pos);
        float reciprocal=out.pos[3]>.00001f?1/out.pos[3]:0;
        out.pos[0]=(out.pos[0]*reciprocal+1)*target_w*.5f;
        out.pos[1]=(1-out.pos[1]*reciprocal)*target_h*.5f;
        out.pos[2]=(out.pos[2]*reciprocal*.5f+.5f)*16777215.f;
        out.uv[0]*=tex_w;out.uv[1]*=tex_h;
        if(enabled[GU_FOG]){float weight=fminf(1,fmaxf(0,(distance-fog_start)/fmaxf(.001f,fog_end-fog_start)));for(int i=0;i<3;i++)out.color[i]=out.color[i]*(1-weight)+((fog_color>>(8*i))&255)/255.f*weight;}
    }
    out.uv[0]=out.uv[0]*tex_scale[0]+tex_offset[0]*tex_w;
    out.uv[1]=out.uv[1]*tex_scale[1]+tex_offset[1]*tex_h;
    if(!is_target(tex_source)||tex_repeat){out.uv[0]/=tex_w?tex_w:1;out.uv[1]/=tex_h?tex_h:1;}
    for(int i=0;i<4;i++)if(!isfinite(out.pos[i])){out.pos[0]=out.pos[1]=-10000;out.pos[2]=0;out.pos[3]=1;break;}
    return out;
}
static MdVertex read_vertex(const void *data,int index,int format){
    if(format&GU_TEXTURE_32BITF)return ((const MdVertex*)data)[index];
    typedef struct {unsigned color;float x,y,z;} Plain;Plain p=((const Plain*)data)[index];return (MdVertex){0,0,p.color,p.x,p.y,p.z};
}
static void submit(int primitive,const MdVertex *v,int count,int format){
    static const unsigned modes[]={0,1,2,4,5,6,7,8};
    reg(NV097_SET_BEGIN_END,modes[primitive]);
    for(int i=0;i<count;){unsigned n=count-i;if(n>10)n=10;uint32_t *p=pb_begin();pb_push(p++,0x40000000|NV097_INLINE_ARRAY,n*12);
        for(unsigned j=0;j<n;j++){Vertex vertex=convert(v[i++],format);memcpy(p,&vertex,sizeof(vertex));p+=12;}pb_end(p);}
    reg(NV097_SET_BEGIN_END,0);
}
void sceGuDrawArray(int primitive,int format,int count,const void *indices,const void *data){
    (void)indices;if(failed||!running||count<=0)return;
    sceKernelDcacheWritebackRange(target,(size_t)target_stride*target_h*4);
    if(clearing){MdVertex a=read_vertex(data,0,format),b=read_vertex(data,count-1,format);unsigned c=a.color;
        c=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);
        reg(NV097_SET_CLEAR_RECT_HORIZONTAL,((unsigned)(b.x-1)<<16)|(unsigned)a.x);reg(NV097_SET_CLEAR_RECT_VERTICAL,((unsigned)(b.y-1)<<16)|(unsigned)a.y);
        reg(NV097_SET_ZSTENCIL_CLEAR_VALUE,0xffffff00);reg(NV097_SET_COLOR_CLEAR_VALUE,c);reg(NV097_CLEAR_SURFACE,0xf3);return;}
    if(!bind_texture())return;
    if(primitive==GU_SPRITES){for(int i=0;i+1<count;i+=2){MdVertex a=read_vertex(data,i,format),b=read_vertex(data,i+1,format),q[4]={a,a,b,b};q[1].x=b.x;q[1].u=b.u;q[3].x=a.x;q[3].u=a.u;submit(GU_TRIANGLE_FAN,q,4,format);}}
    else if(format&GU_TEXTURE_32BITF)submit(primitive,data,count,format);
    else{ /* Preserve strips across packet boundaries: one BEGIN/END. */
        static const unsigned modes[]={0,1,2,4,5,6,7,8};reg(NV097_SET_BEGIN_END,modes[primitive]);
        for(int i=0;i<count;){unsigned n=count-i;if(n>10)n=10;uint32_t *p=pb_begin();pb_push(p++,0x40000000|NV097_INLINE_ARRAY,n*12);for(unsigned j=0;j<n;j++){Vertex v=convert(read_vertex(data,i++,format),format);memcpy(p,&v,sizeof(v));p+=12;}pb_end(p);}reg(NV097_SET_BEGIN_END,0);
    }
}
void sceGuSendCommandi(int cmd,int value){if(cmd==0xd3)clearing=value!=0;}
void sceGuClear(int flags){(void)flags;sceKernelDcacheWritebackRange(target,(size_t)target_stride*target_h*4);reg(NV097_SET_CLEAR_RECT_HORIZONTAL,(target_w-1)<<16);reg(NV097_SET_CLEAR_RECT_VERTICAL,(target_h-1)<<16);reg(NV097_SET_COLOR_CLEAR_VALUE,0);reg(NV097_CLEAR_SURFACE,0xf0);}
void sceGuCopyImage(int fmt,int sx,int sy,int w,int h,int stride,const void *source,int dx,int dy,int dst_stride,void *dest){
    (void)fmt;if(!wait_gpu())return;for(int row=0;row<h;row++)memcpy((unsigned*)dest+(dy+row)*dst_stride+dx,(const unsigned*)source+(sy+row)*stride+sx,w*4);__asm__ volatile("sfence":::"memory");sceKernelDcacheWritebackRange(dest,(size_t)(dy+h)*dst_stride*4);if(!is_target(dest))xv_native_texture(dest);
}
void xv_present_begin(void){if(xv_ram)memset(xv_ram,0,768*480*4);}
const void *xv_pixels(void){return xv_ram;}
