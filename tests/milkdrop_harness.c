#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <sys/mman.h>
#include "milkdrop_warp.h"
#include "milkdrop_decor.h"
#include "milkdrop_preset.h"
#include "cave_visual.h"
typedef struct {float x,y,z,w;} ScePspFVector4;
typedef struct {ScePspFVector4 x,y,z,w;} ScePspFMatrix4;
enum {GU_TRANSFORM_3D=0,GU_PROJECTION=200,GU_VIEW,GU_MODEL,GU_CLIP_PLANES,GU_FOG,GU_COLOR_BUFFER_BIT};
enum {GU_GEQUAL=300,GU_DEPTH_BUFFER_BIT=512};
static int cave_test,cave_draws,matrix_calls,clear_mode,clear_count;
static void sceGuSendCommandi(int command,int argument) {
    assert(command==0xd3 && cave_test);
    if(argument)assert(!clear_mode && argument==(((GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT)<<8)|1));
    else assert(clear_mode);
    clear_mode=argument;
}
static void sceGuSetMatrix(int kind,const ScePspFMatrix4 *m) {
    assert(kind==GU_PROJECTION || kind==GU_VIEW || kind==GU_MODEL);
    if(kind==GU_PROJECTION)assert(m->x.x>0 && m->y.y>0 && m->z.w==-1 && m->w.z<0);
    else assert(m->x.x==1 && m->y.y==1 && m->z.z==1 && m->w.w==1);
    matrix_calls++;
}
static void sceGuDepthRange(int near,int far){assert(near==65535 && far==0);}
static void sceGuDepthMask(int disabled){assert(disabled==1 || (cave_test && disabled==0));}
static void sceGuDepthFunc(int func){assert(func==GU_GEQUAL);}
static void sceGuFog(float near,float far,unsigned int color){assert(near==4 && far==14 && color==0xff000000);}
enum { GU_TEXTURE_32BITF=1, GU_COLOR_8888=2, GU_VERTEX_32BITF=4, GU_TRANSFORM_2D=8,
       GU_SYNC_FINISH=20, GU_SYNC_WHAT_DONE, GU_DIRECT, GU_DEPTH_TEST, GU_CULL_FACE,
       GU_LIGHTING, GU_BLEND, GU_ALPHA_TEST, GU_STENCIL_TEST, GU_SCISSOR_TEST,
       GU_TEXTURE_2D, GU_PSM_8888, GU_TFX_MODULATE, GU_TCC_RGBA, GU_LINEAR,
       GU_REPEAT, GU_CLAMP, GU_TRIANGLES, GU_LINE_STRIP, GU_SPRITES, GU_PSM_5650, GU_LINES, GU_NEAREST, GU_OTHER_COLOR, GU_ONE_MINUS_OTHER_COLOR };
static int effect_texture,filter_src,filter_dst,filter_fix;
static void *md_raw_image;
static int md_height;
static int raw_copies;
static float filter_values[8],filter_source;
static int copy_tile;
static int fade_texture;
static int fade_draws;
static int external_binds;
static void sceKernelDcacheWritebackRange(const void *p,unsigned int n) {assert(p && n);}
static int target_bpp, composition_width, target_changes, texture_offset;
static int raw_target, raw_source;
static int gu_live, starts, syncs, target_offset, target_width, target_height, stride;
static void sceGuDepthBuffer(void *offset,int width) {
    assert(cave_test && width==512 && target_height==256 && target_bpp==2);
    assert((uintptr_t)offset==(unsigned)target_offset+512*256*2);
    assert((uintptr_t)offset+512*256*2<=2097152);
    /* Every frame starts with stale depth/color, including the formerly
     * uncleared rightmost 32 columns. The clear draw must erase all of it. */
    memset((void *)(uintptr_t)(0x44000000+target_offset),0x77,512*256*4);
}
static int fail_init, fail_start;
static int pending_continue,restore_target,finish_syncs,fail_restart;
static int smooth_shading;
static int edram_size = 2*1024*1024, mesh_calls, ring_calls, sprite_calls;
static unsigned char *list_base;
static size_t list_used;
static size_t list_peak;
static unsigned long long test_time = 1000000;
static unsigned long long render_cost;
static uint32_t expected_ring_color;
static int expected_left, expected_top, expected_width, expected_height, covered_width;
static int expected_passes=1;
static int capture_clipped_wave,captured_count;
static MdVertex captured_wave[8];
static const MdVertex *shape_fan;
static int outline_pass, thick_outline_draws;
static int forbid_phase_border;
static unsigned int sceGeEdramGetSize(void) { return edram_size; }
static unsigned long long sceKernelGetSystemTimeWide(void) { return test_time; }
static int sceGuInit(void) {
    assert(!gu_live); if(fail_init) return -1;
    gu_live=1; return 0;
}
static void sceGuTerm(void) { assert(gu_live); gu_live=0;pending_continue=restore_target=0; }
static int sceGuStart(int mode, void *list) {
    smooth_shading=0; /* Do not inherit interpolation from an earlier effect. */
    if(fail_start || (fail_restart && pending_continue)) return -1;
    assert(gu_live && mode==GU_DIRECT); starts++; list_base=list; list_used=0;
    if(pending_continue) {
        assert(syncs>finish_syncs);
        restore_target=target_offset;
        target_offset=0; /* SDK may restore its normal framebuffer. */
        pending_continue=0;return 0;
    }
    covered_width=composition_width=target_changes=0;
    for(int i=0;i<8;i++) filter_values[i]=.25f;
    return 0;
}
static void sceGuSync(int a,int b) { assert(a==GU_SYNC_FINISH && b==GU_SYNC_WHAT_DONE); syncs++; }
static void sceGuFinish(void) { test_time += render_cost;pending_continue=target_changes==1;finish_syncs=syncs; }
static void sceGuDrawBufferList(int format,void *offset,int width) {
    assert(format==GU_PSM_8888 || format==GU_PSM_5650);
    target_bpp=format==GU_PSM_8888?4:2;
    if(restore_target) {
        assert((int)(uintptr_t)offset==restore_target && width==512);
        restore_target=0;
    } else target_changes++;
    target_offset=(int)(uintptr_t)offset; stride=width;
    assert(target_offset==0 || target_offset==1474560 || target_offset==1736704 ||
           target_offset==557056 || target_offset==1081344);
    if (!target_offset) assert(format==GU_PSM_8888 && target_changes==(cave_test?2:3));
}
static void sceGuOffset(int x,int y) { (void)x; (void)y; }
static void sceGuViewport(int x,int y,int w,int h) {
    assert(x==2048 && y==2048); target_width=w; target_height=h;
    assert(target_offset + stride*h*target_bpp <= edram_size);
}
static void sceGuScissor(int x,int y,int w,int h) {
    assert(!x && !y && w==target_width && h==target_height);
}
static void sceGuDisable(int what) { (void)what; }
enum { GU_ADD=100,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,GU_FIX,GU_POINTS,GU_TRIANGLE_FAN,GU_TCC_RGB,GU_SMOOTH };
static void sceGuShadeModel(int mode){assert(mode==GU_SMOOTH);smooth_shading=1;}
static int shade_draws;
static void sceGuBlendFunc(int op,int src,int dst,unsigned int a,unsigned int b) {
    assert(op==GU_ADD);
    assert((src==GU_SRC_ALPHA && dst==GU_ONE_MINUS_SRC_ALPHA && !a && !b) ||
           (src==GU_SRC_ALPHA && dst==GU_FIX && !a && b==0xffffff) ||
           (src==GU_FIX && dst==GU_FIX && a==0xffffff && b==0xffffff) ||
           (src==GU_OTHER_COLOR && dst==GU_FIX && !a && !b) ||
           (src==GU_ONE_MINUS_OTHER_COLOR && (dst==GU_FIX || dst==GU_ONE_MINUS_OTHER_COLOR)));
    filter_src=src; filter_dst=dst;filter_fix=b;
}
static void sceGuEnable(int what) { (void)what; }
static int texture_swizzled,texture_format;
static void sceGuTexMode(int p,int a,int b,int c) {
    assert((p==GU_PSM_8888 || p==GU_PSM_5650) && !a && !b && (c==0 || c==1));
    if(c) assert(p==GU_PSM_8888);
    texture_swizzled=c;texture_format=p;
}
static void sceGuTexImage(int level,int w,int h,int s,const void *texture) {
    uintptr_t offset=(uintptr_t)texture-0x04000000;
    if(cave_test && w==64 && h==64) {
        assert(!level && s==64 && !((uintptr_t)texture&63) && texture_format==GU_PSM_8888);
        return;
    }
    if(w==128 && h==128 && offset>2097152) {
        assert(!level && s==128 && !((uintptr_t)texture&63));
        assert(texture_swizzled && texture_format==GU_PSM_8888);
        const unsigned char *rgba=texture;
        assert(rgba[0]==255 && rgba[1]==180 && rgba[2]==30 && rgba[3]==0);
        assert(rgba[((64/8)*(128/4)+64/4)*128+3]==255);
        external_binds++; fade_texture=effect_texture=0; return;
    }
    assert(!level && (w==512 || w==64) && h==md_height && s==w);
    assert(!texture_swizzled);
    effect_texture=w==64;
    fade_texture=offset>2097152 && texture!=md_raw_image;
    if(texture==md_raw_image) {assert(target_changes<=2);texture_offset=-1;return;}
    if(fade_texture) {assert(w==512 && h==md_height && s==512);return;}
    assert(offset==1474560 || offset==1736704 || offset==557056 || offset==1081344 || offset==1998848 || offset==1605632);
    assert(offset!=(uintptr_t)target_offset);
    texture_offset=(int)offset;
}
static void sceGuTexFunc(int a,int b) { (void)a; (void)b; }
static void sceGuTexFilter(int a,int b) { (void)a; (void)b; }
static int feedback_wrap;
static void sceGuTexWrap(int a,int b) {
    assert(a==b);
    if(target_offset) feedback_wrap=a;
}
static void sceGuTexScale(float a,float b) { (void)a; (void)b; }
static void sceGuTexOffset(float a,float b) { (void)a; (void)b; }
static void sceGuTexFlush(void) {}
static void sceGuTexSync(void) {}
static void sceGuCopyImage(int format,int sx,int sy,int w,int h,int stride,const void *source,
                          int dx,int dy,int ds,void *dest) {
    uintptr_t src=(uintptr_t)source-0x04000000,dst=(uintptr_t)dest-0x04000000;
    assert(format==GU_PSM_8888 || format==GU_PSM_5650);
    if(dest==md_raw_image) {
        assert(format==GU_PSM_5650 && !sx && !sy && !dx && !dy);
        assert(w==512 && h==512 && stride==512 && ds==512);
        assert(target_changes==1 && src==(uintptr_t)raw_target && raw_source==-1);
        raw_source=raw_target;raw_target=-1;raw_copies++;return;
    }
    assert(!sy && !dx && !dy && w==64 && h==md_height && stride==512 && ds==64 && sx>=0 && sx<=448);
    assert(src==(uintptr_t)target_offset && src==(uintptr_t)raw_source && target_changes==2);
    assert(dst==1998848 || dst==1605632);
    assert(dst+64*md_height*(format==GU_PSM_8888?4:2)<=2097152);
    copy_tile=sx/64; filter_source=filter_values[copy_tile];
}
static void *sceGuGetMemory(int bytes) {
    void *result=list_base+list_used;
    list_used+=(bytes+15)&~15;
    if(list_used>list_peak) list_peak=list_used;
    assert(list_used<1572864-32768); /* reserve at least 32 KiB for GU commands */
    return result;
}
static void sceGuDrawArray(int type,int format,int count,const void *indices,const void *data) {
    assert(smooth_shading); /* Transparent shape edges need interpolated alpha. */
    if(forbid_phase_border) assert(type!=GU_LINE_STRIP || count!=33);
    assert(!restore_target);
    const MdVertex *v=data;
    if(cave_test) {
        if(clear_mode) {
            typedef struct {unsigned color;float x,y,z;} ClearVertex;
            const ClearVertex *c=data;
            assert(type==GU_SPRITES && format==14 && count==2 && !indices);
            assert(target_offset && target_width==512 && target_height==256);
            assert(c[0].x==0 && c[0].y==0 && c[1].x==512 && c[1].y==256);
            assert(c[0].z==0 && c[1].z==0 && c[0].color==0xff000000 && c[1].color==0xff000000);
            memset((void *)(uintptr_t)(0x44000000+target_offset),0,512*256*4);
            clear_count++;return;
        }
        if(format==7) {
            const uint16_t *color=(void *)(uintptr_t)(0x44000000+target_offset);
            const uint16_t *depth=color+512*256;
            assert(color[511]==0 && color[512*256-1]==0 && depth[511]==0 && depth[512*256-1]==0);
            assert(!indices && type==GU_TRIANGLES && count>0 && count<=CAVE_MAX_VERTICES && count%3==0);
            assert(!((uintptr_t)data&63));
            for(int i=0;i<count;i++)assert(isfinite(v[i].x) && isfinite(v[i].y) && isfinite(v[i].z));
            cave_draws++;return;
        }
        assert(!indices && (const unsigned char *)data>=list_base &&
               (const unsigned char *)(v+count)<=list_base+list_used);
        assert(format==15 && type==GU_SPRITES && !target_offset);
        assert(v[0].x==expected_left+covered_width && v[0].y==expected_top);
        assert(v[1].y==expected_top+expected_height);
        covered_width+=(int)(v[1].x-v[0].x);assert(covered_width<=expected_width);
        return;
    }
    MdVertex unpacked[4*MD_CUSTOM_POINTS];
    assert((format==15 || format==14) && !indices);
    if(format==15) {
        assert((const unsigned char *)data>=list_base);
        assert((const unsigned char *)(v+count)<=list_base+list_used);
    }
    if(format==14) {
        struct Plain {unsigned int color;float x,y,z;};
        const struct Plain *p=data;
        assert(type==GU_LINE_STRIP || type==GU_POINTS || type==GU_LINES);
        assert(count>0 && count<=4*MD_CUSTOM_POINTS);
        assert((const unsigned char *)data>=list_base &&
               (const unsigned char *)(p+count)<=list_base+list_used);
        for(int i=0;i<count;i++) unpacked[i]=(MdVertex){.color=p[i].color,.x=p[i].x,.y=p[i].y,.z=p[i].z};
        v=unpacked;
        if(capture_clipped_wave) {
            assert(count<=8);captured_count=count;
            memcpy(captured_wave,v,count*sizeof(*v));
        }
    }
    if(type==GU_TRIANGLES && count==6 && target_changes==2) {
        assert(target_changes==2 && target_offset==raw_source && texture_offset==raw_target);
        assert(v[0].x==0 && v[0].y==0 && v[4].x==512 && v[4].y==md_height);
        assert(v[1].color==v[3].color && v[2].color==v[5].color);
        assert(v[0].color!=v[1].color || v[0].color!=v[2].color);
        composition_width+=512;shade_draws++;
        assert(composition_width<=512*expected_passes);
    }
    else if(type==GU_TRIANGLES && count!=MD_MESH_VERTICES) {
        assert(target_changes==1 && count>0 && count<=15*MD_SHAPE_SIDES && count%3==0);
        for(int i=0;i<count;i++)assert(v[i].x>=0 && v[i].x<=512 && v[i].y>=0 && v[i].y<=md_height);
    }
    else if(type==GU_TRIANGLES) {
        assert(count==MD_MESH_VERTICES && target_changes==1);
        assert(target_width==512 && target_height==md_height);
        raw_target=target_offset; raw_source=texture_offset;
        assert(raw_target!=raw_source);
        float right=0;
        for(int i=0;i<count;i++) if(v[i].x>right) right=v[i].x;
        assert(right==512);
        mesh_calls++;
    }
    else if(type==GU_TRIANGLE_FAN) {
        assert(count>=5 && count<=MD_SHAPE_SIDES+2);
        shape_fan=v; outline_pass=0;
    }
    else if(format==14 && (type==GU_LINES || type==GU_POINTS)) {
        assert(type==GU_POINTS || count%2==0);ring_calls++;
        for(int i=0;i<count;i++)assert(v[i].x>=0 && v[i].x<=512 && v[i].y>=0 && v[i].y<=md_height);
    }
    else if(type==GU_LINES) { assert(count<=MD_MOTION_MAX_VERTICES && count%2==0); }
    else if(type==GU_LINE_STRIP || type==GU_POINTS) {
        assert(count==MD_CUSTOM_POINTS || count==2*MD_CUSTOM_POINTS-1 || count==512 || count==1023 || count==192 || count==383 || count==64 || count==127 || count==97 || count==170 || count==240 || count==241 || count==256 || count==480 || (count>=4 && count<=MD_SHAPE_SIDES+1)); ring_calls++;
        if((count<=33 || count==MD_SHAPE_SIDES+1) && count!=16 && count!=31) {
            static const float dx[]={0,1,1,0},dy[]={0,0,-1,-1};
            assert(shape_fan && outline_pass<4);
            for(int i=0;i<count;i++) {
                assert(v[i].x==shape_fan[i+1].x+dx[outline_pass]);
                assert(v[i].y==shape_fan[i+1].y+dy[outline_pass]);
            }
            assert(v[0].x==v[count-1].x && v[0].y==v[count-1].y);
            if(outline_pass) thick_outline_draws++;
            outline_pass++;
        }
        if(expected_ring_color) for(int i=0;i<count;i++) assert(v[i].color==expected_ring_color);
    }
    else {
        assert(type==GU_SPRITES && count==2); sprite_calls++;
        if(target_offset) {
            if(fade_texture && target_changes==2) {fade_draws++;return;}
            if(effect_texture && target_changes==2) {
                float d=filter_values[copy_tile],s=filter_source;
                if(filter_src==GU_ONE_MINUS_OTHER_COLOR && filter_dst==GU_FIX && !filter_fix) s=1; /* invert, untextured */
                float sf=filter_src==GU_OTHER_COLOR?d:1-d;
                float df=filter_dst==GU_ONE_MINUS_OTHER_COLOR?1-s:filter_fix?1:0;
                filter_values[copy_tile]=s*sf+d*df;
                return;
            }
            if(target_changes==2) {
                assert(texture_offset!=target_offset);
                assert(target_offset==raw_source && texture_offset==raw_target);
                assert(v[0].x==composition_width%512 && v[0].y==0 && v[1].y==md_height);
                composition_width+=(int)(v[1].x-v[0].x);
                assert(composition_width<=512*expected_passes);
            }
            return; /* feedback borders or offscreen composition */
        }
        assert(composition_width==512*expected_passes);
        assert(texture_offset==raw_source);
        assert(v[0].x==expected_left+covered_width%expected_width && v[0].y==expected_top);
        assert(v[1].y==expected_top+expected_height);
        covered_width+=(int)(v[1].x-v[0].x);
        assert(covered_width<=expected_width);
    }
    for(int i=0;i<count;i++) {
        assert(isfinite(v[i].u) && isfinite(v[i].v));
        assert(isfinite(v[i].x) && isfinite(v[i].y));
        if(type==GU_TRIANGLE_FAN || (type==GU_LINE_STRIP &&
           (count<=33 || count==MD_SHAPE_SIDES+1) && count!=16 && count!=31)) {
            /* Only interior shapes use the fan/line-strip fast path now. */
            assert(target_width==512 && target_height==md_height && target_changes==1);
            assert(v[i].x>=0 && v[i].x<=512);
            assert(v[i].y>=0 && v[i].y<=md_height);
        } else if(type==GU_LINES || count==170 || count==240 || count==256 || count==480) {
            /* Mode 4 intentionally moves its line beyond the texture edge;
             * the viewport scissor clips it, not coordinate clamping. */
            assert(target_width==512 && target_height==md_height && target_changes==1);
            assert(v[i].x>-2048 && v[i].x<2048);
            assert(v[i].y>-2048 && v[i].y<2048);
        } else if(count==192 || count==383) {
            /* The new custom wave reaches x=1. Thick copies extend one pixel
             * past the feedback edge and are clipped by the same scissor. */
            assert(target_width==512 && target_height==md_height && target_changes==1);
            assert(v[i].x>=-1 && v[i].x<=target_width+1);
            assert(v[i].y>=-1 && v[i].y<=target_height+1);
        } else {
            assert(v[i].x>=0 && v[i].x<=target_width);
            assert(v[i].y>=0 && v[i].y<=target_height);
        }
    }
}
#include <malloc.h>
static int fail_feedback_allocation;
static void *test_memalign(size_t alignment,size_t size) {
    if(fail_feedback_allocation && size==512*512*2) {
        fail_feedback_allocation=0;return NULL;
    }
    return memalign(alignment,size);
}
#define memalign test_memalign
/* GU_ADAPTER */
#undef memalign
int main(int argc,char **argv) {
    assert(argc==40 || argc==41 || (argc==2 && !strcmp(argv[1],"--cave")));
    md_profile_reset(1);
    md_profile_select("host render integration",0,0,3);
    MdVertex mesh[MD_MESH_VERTICES], ring[97];
    MdPreset identity={1,0,0,1,1,1,0,0,.5f,.5f,1,1,1};
    unsigned char bands[12];
    unsigned char *vram=(void *)0x44000000;
    assert(mmap(vram,edram_size,PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==vram);
    memset(vram,0xa5,edram_size);
    if(argc==2) {
        cave_test=1;memset(bands,75,sizeof(bands));
        int frames=32,mode=5;
        for(int resolution=0;resolution<2;resolution++)for(int tv=0;tv<2;tv++)for(int full=0;full<2;full++) {
            md_high_resolution=resolution;assert(md_start());
            expected_left=full?0:tv?26:38;expected_top=full?0:tv?86:74;
            expected_width=(full?(tv?720:480):(tv?534:344))-expected_left;
            expected_height=(full?(tv?480:272):(tv?294:149))-expected_top;
            int previous=cave_draws;
            for(int f=0;f<frames;f++) {
                test_time+=100000;assert(md_frame(tv,full,bands,75,test_time,mode)==1);
                assert(covered_width==expected_width);
                int before=starts;assert(md_frame(tv,full,bands,75,test_time,mode)==1 && starts==before);
            }
            assert(cave_draws>previous);
            md_stop();assert(!gu_live && !md_list && !cave_scene);
        }
        assert(matrix_calls==frames*8*3 && clear_count==frames*8 && !clear_mode);
        printf("Cave: LCD/TV, window/full, resolutions, throttle and teardown OK; peak list %zu bytes\n",list_peak);
        return 0;
    }
    md_warp_mesh(mesh,&identity,0);
    for(int i=0;i<MD_MESH_VERTICES;i++) {
        assert(fabsf(mesh[i].u-mesh[i].x-.5f)<.0001f);
        assert(fabsf(mesh[i].v-mesh[i].y-.5f)<.0001f);
    }
    identity.dx=.01f; identity.dy=-.02f;
    md_warp_mesh(mesh,&identity,0);
    for(int i=0;i<MD_MESH_VERTICES;i++) {
        assert(fabsf(mesh[i].u-(mesh[i].x+.5f-.01f*MD_TEXTURE))<.0001f);
        assert(fabsf(mesh[i].v-(mesh[i].y+.5f+.02f*MD_TEXTURE))<.0001f);
    }
    identity.zoom=1.2f; identity.zoomexp=1.5f; identity.cx=.2f; identity.cy=.7f;
    identity.sx=2; identity.sy=.5f; identity.rotation=.1f;
    md_warp_mesh(mesh,&identity,0);
    {
        double z=pow(1.2,pow(1.5,sqrt(2.0)*2-1));
        double u=(-.5/z+.5-.2)/2+.2, v=(-.5/z+.5-.7)/.5+.7;
        double a=u-.2,b=v-.7;
        assert(fabs(mesh[0].u-((a*cos(.1)-b*sin(.1)+.2-.01)*256+.5))<.0002);
        assert(fabs(mesh[0].v-((a*sin(.1)+b*cos(.1)+.7+.02)*256+.5))<.0002);
    }
    edram_size=1024*1024;
    assert(!md_start() && !gu_live);
    edram_size=2*1024*1024;
    fail_init=1; assert(!md_start() && !md_list && !gu_live); fail_init=0;
    assert(md_start()); fail_start=1;
    memset(bands,0,sizeof(bands));
    assert(!md_frame(0,0,bands,0,test_time,0) && !md_list && !gu_live);
    fail_start=0;
    md_custom_preset.warp=md_presets[0];
    md_custom_preset.red=1; md_custom_preset.green=.5f; md_custom_preset.blue=0;
    for(int tv=0;tv<2;tv++) for(int full=0;full<2;full++) {
        expected_left=full ? 0 : tv ? 26 : 38;
        expected_top=full ? 0 : tv ? 86 : 74;
        expected_width=full ? (tv ? 720 : 480) : tv ? 508 : 306;
        expected_height=full ? (tv ? 480 : 272) : tv ? 208 : 75;
        assert(md_start() && md_start());
        for(int i=0;i<557056;i++) assert(vram[i]==0xa5);
        for(int preset=0;preset<4;preset++) {
            expected_ring_color=preset==3 ? 0xff007fffU : 0;
            for(int frame=0;frame<120;frame++) {
                int calls=starts;
                for(int i=0;i<12;i++) bands[i]=(frame*7+i*9)%101;
                test_time+=100000;
                md_frame(tv,full,bands,frame%101,test_time,preset);
                assert(starts==calls+1);
                assert(covered_width==expected_width);
                md_frame(tv,full,bands,50,test_time,preset);
                assert(starts==calls+1);
                md_audio_ring(ring,bands,frame%101,frame*.1f,preset);
                assert(ring[0].x==ring[96].x && ring[0].y==ring[96].y);
            }
        }
        md_stop(); md_stop(); assert(!gu_live && !md_list);
    }
    assert(mesh_calls==1920 && ring_calls>0 && sprite_calls>mesh_calls);
    assert(syncs>=starts);
    /* Title height changes only the top, never the other three TV edges. */
    expected_ring_color=0;
    assert(md_start());
    expected_left=26; expected_top=84; expected_width=508; expected_height=210;
    md_set_tv_title_bottom(79);
    assert(md_frame(1,0,bands,0,test_time,0));
    expected_top=100; expected_height=194;
    md_set_tv_title_bottom(95);
    assert(md_frame(1,0,bands,0,test_time,0));
    md_stop();
    md_set_tv_title_bottom(81);
    expected_ring_color=0;
    expected_left=38; expected_top=74; expected_width=306; expected_height=75;
    assert(md_start());
    render_cost=50000;
    assert(md_frame(0,0,bands,0,test_time,0));
    assert(md_next==test_time+100000);
    {
        int calls=starts;
        test_time+=99999;
        assert(md_frame(0,0,bands,0,test_time,0) && starts==calls);
        test_time++;
        assert(md_frame(0,0,bands,0,test_time,0) && starts==calls+1);
    }
    md_stop();
    assert(md_start());
    render_cost=0;
    assert(md_frame(0,0,bands,0,test_time,0));
    assert(md_next==test_time+50000);
    {
        const unsigned long long costs[]={16667,24999,25000,25001,50000};
        for(unsigned int i=0;i<sizeof(costs)/sizeof(costs[0]);i++) {
            test_time=md_next;render_cost=costs[i];
            assert(md_frame(0,0,bands,0,test_time,0));
            unsigned long long idle=costs[i]>25000?costs[i]*2:50000;
            assert(md_next==test_time+idle);
        }
        render_cost=0;
    }
    {
        int calls=starts;
        expected_left=expected_top=0; expected_width=480; expected_height=272;
        assert(md_frame(0,1,bands,0,test_time,0) && starts==calls+1);
        expected_left=38; expected_top=74; expected_width=306; expected_height=75;
        assert(md_frame(0,0,bands,0,test_time,0) && starts==calls+2);
    }
    md_stop();
    for(int i=1998848;i<edram_size;i++) assert(vram[i]==0xa5);
    /* Music inputs reach actual ring vertices; throttling never advances them. */
    assert(pm_compile(&md_custom_preset.program,
        "wave_r=psp_low; wave_g=psp_mid; wave_b=psp_high;",2)==PM_OK);
    for(int tv=0;tv<2;tv++) {
        expected_left=tv ? 26 : 38; expected_top=tv ? 86 : 74;
        expected_width=tv ? 508 : 306; expected_height=tv ? 208 : 75;
        assert(md_start());
        assert(!md_signal_state.ready);
        for(int i=0;i<12;i++) bands[i]=i<4 ? 100 : i<8 ? 50 : 0;
        expected_ring_color=0xff007fffU;
        test_time+=100000;
        assert(md_frame(tv,0,bands,75,test_time,3)==1);
        assert(md_signal_state.signal.values[4]==1);
        unsigned long long tick=md_signal_state.tick;
        memset(bands,0,sizeof(bands));
        assert(md_frame(tv,0,bands,0,test_time,3)==1);
        assert(md_signal_state.tick==tick && md_signal_state.signal.values[0]==1);
        test_time+=100000;
        assert(md_frame(tv,0,bands,0,test_time,3)==1);
        assert(md_signal_state.signal.values[0]==0);
        assert(md_signal_state.signal.values[4]>0 && md_signal_state.signal.values[4]<1);
        md_stop();
    }
    expected_left=38; expected_top=74; expected_width=306; expected_height=75;
    pm_program_free(&md_custom_preset.program);
    /* A runtime formula failure must happen before opening a GU list. */
    assert(pm_compile(&md_custom_preset.program,"rot=megabuf(1/(time-time));",7)==PM_OK);
    assert(md_start());
    {
        int calls=starts;
        assert(md_frame(0,0,bands,50,test_time,3)==-1);
        assert(starts==calls && md_runtime_error.line==7);
        md_stop();
        assert(!gu_live && !md_list);
    }
    /* Circular PCM waveform, translation, clamp and two-pass gamma on LCD/TV. */
    pm_program_free(&md_custom_preset.program);
    md_custom_preset.wave_mode=0; md_custom_preset.gamma=2;
    md_custom_preset.wave_scale=1; md_custom_preset.wave_smoothing=.75f;
    md_custom_preset.wave_alpha=1; md_custom_preset.wrap=0;
    md_custom_preset.warp.dx=.01f; md_custom_preset.warp.dy=-.01f;
    expected_ring_color=0xff007fff; expected_passes=2;
    for(int tv=0;tv<2;tv++) for(int full=0;full<2;full++) {
        expected_left=full?0:tv?26:38; expected_top=full?0:tv?86:74;
        expected_width=full?(tv?720:480):tv?508:306;
        expected_height=full?(tv?480:272):tv?208:75;
        assert(md_start());
        test_time+=100000;
        assert(md_frame(tv,full,bands,0,test_time,3)==1);
        assert(md_wave_capture && covered_width==expected_width);
        assert(feedback_wrap==GU_CLAMP);
        short pcm[1152];
        for(int i=0;i<1152;i++) pcm[i]=(short)(sin(i*.08)*30000);
        visualization_pcm_publish(pcm,576);
        test_time+=100000;
        assert(md_frame(tv,full,bands,75,test_time,3)==1);
        assert(md_right[9]==pcm[19]);
        md_stop(); assert(!md_wave_capture);
    }
    /* Maximum static layer combination: stays within one fixed GU list. */
    for(int i=0;i<5;i++) md_custom_preset.effects[i]=1;
    for(int i=0;i<MD_SHAPES;i++) md_custom_preset.shape_instances[i]=MD_SHAPE_INSTANCES;
    for(int i=0;i<MD_CUSTOM_WAVES;i++) md_custom_preset.waves[i]=(MdCustomWave){.enabled=1,.samples=MD_CUSTOM_POINTS,.thick=1,.scaling=.1f,.r=1,.g=1,.b=1,.a=1};
    expected_ring_color=0; expected_passes=8;
    md_custom_preset.gamma=4;
    MdDecor *decor=&md_custom_preset.decor;
    decor->echo_zoom=2; decor->echo_alpha=.4f;
    decor->wave_thick=decor->wave_dots=decor->wave_additive=decor->wave_brighten=1;
    decor->wave_mod_alpha=1; decor->wave_mod_start=.75f; decor->wave_mod_end=.95f;
    decor->outer=(MdBorder){.03f,1,0,0,.5f};
    decor->inner=(MdBorder){.03f,0,0,1,.5f};
    for(int i=0;i<MD_SHAPES;i++) decor->shapes[i]=(MdShape){
        .enabled=1,.sides=1e30f,.additive=i%2,.textured=i%2,.x=.5f,.y=.5f,.rad=.4f,
        .tex_zoom=1,.r=1,.g=.3f,.b=.2f,.a=.5f,.r2=.2f,.g2=.3f,.b2=1,.a2=.1f,
        .border_r=1,.border_g=1,.border_b=1,.border_a=.3f,.thick_outline=1};
    for(int tv=0;tv<2;tv++) for(int full=0;full<2;full++) {
        expected_left=full?0:tv?26:38; expected_top=full?0:tv?86:74;
        expected_width=full?(tv?720:480):tv?508:306;
        expected_height=full?(tv?480:272):tv?208:75;
        assert(md_start());
        for(int mode=0;mode<=8;mode++) for(int orientation=0;orientation<4;orientation++) {
            md_custom_preset.wave_mode=mode;
            md_custom_preset.shader_amount=(mode&1)?1:0;
            decor->echo_zoom=(orientation&1)?.25f:2;
            float motion[]={.5f,1,1,1,64,48,0,0,2};
            memcpy(md_custom_preset.motion,motion,sizeof(motion));
            decor->echo_orient=orientation;
            if(mode==4 && orientation==1) md_begin_preset(1500);
            test_time+=100000;
            int before_shade=shade_draws;
            assert(md_frame(tv,full,bands,75,test_time,3)==1);
            assert(shade_draws-before_shade==((mode&1)?expected_passes:0));
            assert(covered_width==expected_width);
            float reference=.25f;
            if(md_preset_state.effects[1]) reference=1-(1-reference)*(1-reference);
            if(md_preset_state.effects[2]) reference*=reference;
            if(md_preset_state.effects[3]) reference=2*reference*(1-reference);
            if(md_preset_state.effects[4]) reference=1-reference;
            for(int i=0;i<8;i++) assert(fabsf(filter_values[i]-reference)<.00001f);
        }
        md_stop();
    }
    /* More than one list, including all maximum layers. Reuse is safe only
     * after sync; the stub also requires restoring the feedback framebuffer. */
    for(int i=0;i<MD_SHAPES;i++)md_custom_preset.shape_instances[i]=MD_SHAPE_MAX_INSTANCES;
    for(int tv=0;tv<2;tv++) for(int full=0;full<2;full++) {
        expected_left=full?0:tv?26:38;expected_top=full?0:tv?86:74;
        expected_width=full?(tv?720:480):tv?508:306;
        expected_height=full?(tv?480:272):tv?208:75;
        assert(md_start());int before=starts;
        test_time+=100000;
        assert(md_frame(tv,full,bands,75,test_time,3)==1);
        int shape_lists=MD_SHAPES*MD_SHAPE_MAX_INSTANCES/MD_SHAPE_BATCH;
        assert(starts-before>=shape_lists && starts-before<=shape_lists+MD_CUSTOM_WAVES+1);
        assert(covered_width==expected_width);
        md_stop();assert(!md_shape_frame);
    }
    /* Maximum clipped shapes plus all other layers: weighted batching must
     * retain list headroom on both targets and release all scratch on stop. */
    for(int i=0;i<MD_SHAPES;i++) {decor->shapes[i].x=-.5f;decor->shapes[i].rad=4;}
    for(int tv=0;tv<2;tv++) for(int full=0;full<2;full++) {
        expected_left=full?0:tv?26:38;expected_top=full?0:tv?86:74;
        expected_width=full?(tv?720:480):tv?508:306;
        expected_height=full?(tv?480:272):tv?208:75;
        assert(md_start());test_time+=100000;
        assert(md_frame(tv,full,bands,75,test_time,3)==1);
        assert(covered_width==expected_width);md_stop();
    }
    assert(md_start());fail_restart=1;test_time+=100000;
    assert(md_frame(1,1,bands,75,test_time,3)==0);
    assert(!md_list && !md_shape_frame && !gu_live);fail_restart=0;
    /* Actual file/parser/interpreter/renderer path with alternating music. */
    MdFileError demo_error;
    expected_passes=4; expected_ring_color=0;
    for(int fixture=1;fixture<argc;fixture++) {
    expected_passes=fixture<=23?4:(fixture==28 || fixture==29)?2:1;
    int explosion=strstr(argv[fixture],"Geiss - Explosion nz+")!=NULL;
    int phase_memory=strstr(argv[fixture],"phase-memory-demo")!=NULL;
    if(explosion)expected_passes=2;
    assert(md_load_preset(argv[fixture],&md_custom_preset,&demo_error)==MD_FILE_OK);
    forbid_phase_border=phase_memory;
    if(phase_memory)md_custom_preset.decor.shapes[0].border_a=-.5f;
    for(int tv=0;tv<2;tv++) for(int full=0;full<2;full++) {
        expected_left=full?0:tv?26:38; expected_top=full?0:tv?86:74;
        expected_width=full?(tv?720:480):tv?508:306;
        expected_height=full?(tv?480:272):tv?208:75;
        assert(md_start());
        assert(!md_preset_state.ready);
        for(int frame=0;frame<((explosion || phase_memory)?30:600);frame++) {
            memset(bands,frame%2?90:10,sizeof(bands));
            test_time+=100000;
            assert(md_frame(tv,full,bands,75,test_time,3)==1);
            assert(covered_width==expected_width);
            assert(md_preset_state.ready);
            if(phase_memory) {
                assert(md_shape_frame->count[0]==1);
                const MdShape *shape=&md_shape_frame->shapes[0][0];
                assert(shape->x>=.25f && shape->x<=.75f);
                assert(shape->y>=.3f && shape->y<=.7f);
                assert(shape->a>.7f && shape->a2==0 && smooth_shading);
            }
            if(fixture==33)assert(md_shape_frame->count[0]==128);
            if(explosion)assert(md_shape_frame->count[1]==311);
            if(fixture==27) {
                assert(md_images_ready && md_images[0].pixels && external_binds>0);
                if(frame==20) {md_begin_preset(500);assert(!md_images[0].pixels);}
            }
            if(fixture==15) {
                short pcm[2048];
                assert(md_preset_state.wave_mode>=0 && md_preset_state.wave_mode<=8);
                assert(md_wave_capture==(md_preset_state.wave_mode==8?3:md_preset_state.wave_mode?2:1));
                for(int i=0;i<1024;i++) { pcm[i*2]=(short)(sin(i*.07)*20000); pcm[i*2+1]=(short)(cos(i*.05)*18000); }
                visualization_pcm_publish(pcm,1024);
            }
            if(fixture==17) {
                short pcm[1152];
                assert(md_wave_capture==2 && md_custom_geometry[0].count==64 && md_custom_geometry[1].count==64);
                for(int i=0;i<1152;i++) pcm[i]=(short)(sin(i*.05)*20000);
                visualization_pcm_publish(pcm,576);
            }
            if(fixture==19 || fixture==20) {
                short pcm[2048];
                assert(md_wave_capture==(fixture==19?5:2));
                for(int i=0;i<1024;i++) {pcm[2*i]=(short)(sin(i*.19634954)*20000);pcm[2*i+1]=(short)(sin(i*.39269908)*18000);}
                visualization_pcm_publish(pcm,1024);
                if(fixture==19 && frame) {assert(md_bins_left[32]>.5f);assert(md_bins_right[64]>.5f);}
            }
            if(fixture==21) {
                float reference=.25f;
                if(md_preset_state.effects[1]) reference=1-(1-reference)*(1-reference);
                if(md_preset_state.effects[2]) reference*=reference;
                if(md_preset_state.effects[3]) reference=2*reference*(1-reference);
                if(md_preset_state.effects[4]) reference=1-reference;
                for(int i=0;i<8;i++) assert(fabsf(filter_values[i]-reference)<.00001f);
            }
            if(fixture==22 && frame==20) {
                md_begin_preset(1500);assert(md_fade_image);
            }
            if(fixture==22 && frame==21) assert(fade_draws>0 && md_fade_image);
            if(fixture==22 && frame==50) assert(!md_fade_image);
            if(fixture==23) assert(md_custom_geometry[0].count==512);
            if(fixture==2) assert(fabsf(md_preset_state.q[0]-.7f)<.00001f);
            if(md_custom_preset.wave_mode==4 || md_custom_preset.wave_mode==1) {
                short pcm[1152];
                assert(md_wave_capture==2);
                if(frame) { assert(md_right[9]==1234); assert(md_left[9]==-2345); }
                for(int i=0;i<576;i++) { pcm[2*i]=-2345; pcm[2*i+1]=1234; }
                visualization_pcm_publish(pcm,576);
            }
            if(md_custom_preset.wave_mode==8) {
                short pcm[2048]; assert(md_wave_capture==(fixture==18?4:3));
                if(frame) assert(md_spectrum[9]==4321);
                for(int i=0;i<1024;i++) { pcm[2*i]=4321; pcm[2*i+1]=-123; }
                visualization_pcm_publish(pcm,1024);
            }
        }
        md_stop();
        assert(!md_images_ready && !md_images[0].pixels);
    }
    }
    forbid_phase_border=0;
    /* Layout changes bypass the frame throttle and reset only feedback.
     * Scanout format stays 32-bit even when TV feedback becomes RGB565. */
    /* Four independent slots, then a partial load failure: neither case may
     * leave allocations alive after a preset reset or corrupt the GU list. */
    assert(md_load_preset(argv[27],&md_custom_preset,&demo_error)==MD_FILE_OK);
    assert(md_start());
    for(int i=1;i<MD_SHAPES;i++) strcpy(md_custom_preset.texture_path[i],md_custom_preset.texture_path[0]);
    assert(md_images_prepare());
    for(int i=0;i<MD_SHAPES;i++) assert(md_images[i].pixels);
    md_begin_preset(0);
    strcpy(md_custom_preset.texture_path[1],"/nonexistent-psp-texture.png");
    assert(!md_images_prepare());
    assert(md_runtime_error.code==MD_FILE_IO && !strcmp(md_runtime_error.key,"psp_texture_1"));
    for(int i=0;i<MD_SHAPES;i++) assert(!md_images[i].pixels);
    memset(md_custom_preset.texture_path,0,sizeof(md_custom_preset.texture_path));
    md_stop();
    expected_passes=1;
    assert(md_start());
    for(int i=0;i<12;i++) {
        int tv=i%2;
        expected_left=expected_top=0;
        expected_width=tv?720:480; expected_height=tv?480:272;
        int calls=starts;
        assert(md_frame(tv,1,bands,0,test_time,0)==1 && starts==calls+1);
        assert(md_texture_base==(tv?1474560:557056));
        assert(md_texture_bytes==524288 && md_height==512);
        assert(md_pixel_format==GU_PSM_5650);
        assert((md_raw_image!=NULL)==tv);
        assert(md_front==1 && raw_source==md_texture_base);
        assert(target_offset==0 && target_bpp==4);
    }
    md_stop();
    for(int i=0;i<557056;i++) assert(vram[i]==0xa5);
    for(int i=2064384;i<edram_size;i++) assert(vram[i]==0xa5);
    assert(raw_copies>0 && !md_raw_image);
    /* Allocation failure retains the old TV layout and can recover on the
     * next output change. Audio owns none of these resources. */
    assert(md_start());fail_feedback_allocation=1;
    expected_left=expected_top=0;expected_width=720;expected_height=480;
    assert(md_frame(1,1,bands,0,test_time,0)==1);
    assert(!md_raw_image && md_height==256 && md_texture_bytes==262144);
    assert(!fail_feedback_allocation);
    expected_width=480;expected_height=272;
    assert(md_frame(0,1,bands,0,test_time,0)==1);
    assert(md_height==512 && !md_raw_image);
    expected_width=720;expected_height=480;
    assert(md_frame(1,1,bands,0,test_time,0)==1);
    assert(md_height==512 && md_raw_image);
    /* Change quality with the same output, without restarting GU/the app. */
    for(int tv=0;tv<=1;tv++) {
        expected_width=tv?720:480;expected_height=tv?480:272;
        for(int high=0;high<=1;high++) {
            md_high_resolution=high;
            assert(md_frame(tv,1,bands,0,test_time,0)==1);
            assert(md_height==(high?512:256));
            assert(md_pixel_format==(!tv&&!high?GU_PSM_8888:GU_PSM_5650));
            assert((md_raw_image!=NULL)==(tv&&high));
        }
    }
    md_stop();
    md_texture_base=557056; /* Direct list-restart checks use LCD storage. */
    assert(md_profiles[0].frames>0);
    assert(md_profiles[0].skipped>0);
    char profile[1024];
    assert(md_profile_report(0,profile,sizeof(profile)));
    assert(strstr(profile,"geometry_submit avg_us=") && strstr(profile,"gpu_wait avg_us="));
    printf("Visualization maximum vertex storage: %zu / %d bytes\n",list_peak,MD_LIST_BYTES);
    assert(thick_outline_draws>0);
    for(int failure=0;failure<2;failure++) {
        assert(md_start());assert(sceGuStart(GU_DIRECT,md_list)>=0);
        md_target(MD_TEXTURE_BASE+(1-md_front)*MD_TEXTURE_BYTES,MD_WIDTH,MD_WIDTH,MD_HEIGHT);
        MdVertex *wave=sceGuGetMemory(4*sizeof(*wave));
        wave[0]=(MdVertex){.color=0xffffffff,.x=-20,.y=10};
        wave[1]=(MdVertex){.color=0xffffffff,.x=100,.y=10};
        wave[2]=(MdVertex){.color=0xffffffff,.x=400,.y=20};
        wave[3]=(MdVertex){.color=0xffffffff,.x=540,.y=20};
        capture_clipped_wave=1;fail_restart=failure;
        assert(md_draw_wave(GU_LINE_STRIP,wave,4,2,1)==!failure);
        if(!failure) {
            assert(captured_count==4); /* no artificial bridge across split */
            assert(captured_wave[0].x==0 && captured_wave[1].x==100);
            assert(captured_wave[2].x==400 && captured_wave[3].x==512);
            assert(captured_wave[0].y==9 && captured_wave[3].y==19);
            sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
        }
        capture_clipped_wave=fail_restart=0;md_stop();assert(!md_list);
    }
    munmap(vram,edram_size);
    return 0;
}
