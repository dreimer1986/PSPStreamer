/* Native cached isosurface geometry; shares the music renderer's GU owner. */
#include "cave_clip.h"
#include "cave_style.h"
#include "cave_textures.h"
static CaveScene *cave_scene;
#include "cave_ship_data.h"
void md_cave_control(int toggle,int x,int y,int throttle) {
    cave_flight_input(cave_scene,toggle,x,y,throttle);
}
static void cave_draw_ship(int width,int height) {
    if(!cave_scene || !cave_scene->flight)return;
    /* Draw the third-person ship in camera space, inside the renderer's
     * reserved 64 KiB tail. No buffers, allocation or draw calls when off. */
    _Static_assert(sizeof(cave_ship_mesh)+2048<65536,"Ship exceeds GU tail reserve");
    MdVertex *ship=sceGuGetMemory(sizeof(cave_ship_mesh));
    float roll=-cave_scene->flight_axis_x*.35f,pitch=cave_scene->flight_axis_y*.18f;
    float cr=cosf(roll),sr=sinf(roll),cp=cosf(pitch),sp=sinf(pitch);
    for(int i=0;i<CAVE_SHIP_VERTICES;i++) {
        ship[i]=cave_ship_mesh[i];
        float x=ship[i].x,y=ship[i].y*cp-ship[i].z*sp,z=ship[i].y*sp+ship[i].z*cp;
        ship[i].x=x*cr-y*sr;ship[i].y=x*sr+y*cr-.4f;ship[i].z=z-2.4f;
    }
    ScePspFMatrix4 identity={.x={1,0,0,0},.y={0,1,0,0},.z={0,0,1,0},.w={0,0,0,1}};
    /* Easter-egg overlay stays readable as the original tunnel FOV breathes.
     * Its model is in camera units, not source tunnel coordinates. */
    ScePspFMatrix4 projection={.x={1.25f*height/width,0,0,0},.y={0,1.25f,0,0},
        .z={0,0,-1.006689f,-1},.w={0,0,-.200669f,0}};
    sceGuSetMatrix(GU_PROJECTION,&projection);
    sceGuSetMatrix(GU_VIEW,&identity);
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuDisable(GU_FOG);
    sceGuDepthMask(0);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,CAVE_SHIP_VERTICES,NULL,ship);
}
static uint32_t cave_pixels[7][CAVE_TEXTURE*CAVE_TEXTURE] __attribute__((aligned(64)));
static int cave_texture_ready;
static MdImage cave_external[CAVE_TEXTURE_BANKS];
static int cave_external_next;
static unsigned cave_external_mask;
static void cave_textures_clear(void) {
    for(int i=0;i<CAVE_TEXTURE_BANKS;i++)md_image_free(cave_external+i);
    cave_external_next=0;cave_external_mask=0;
}
static void cave_textures_step(void) {
    if(cave_external_next>=CAVE_TEXTURE_BANKS)return;
    int bank=cave_external_next++;
    /* One bounded decode per visual update, before starting the GU list.
     * Existing music UI priority is below the audio workers. */
    if(cave_texture_load("monkey",bank,cave_external+bank)) {
        MdImage *image=cave_external+bank;
        sceKernelDcacheWritebackRange(image->pixels,image->width*image->height*4);
        cave_external_mask|=1U<<bank;
    }
    if(cave_external_next==CAVE_TEXTURE_BANKS) {
        char message[80];snprintf(message,sizeof(message),"Cave external textures: %02X (missing/invalid use fallback)",cave_external_mask);
        md_trace(message);
    }
}
static void cave_bind(int bank) {
    sceGuEnable(GU_TEXTURE_2D);
    MdImage *image=cave_external+bank;
    if(image->pixels) {
        sceGuTexMode(GU_PSM_8888,0,0,1);
        sceGuTexImage(0,image->width,image->height,image->width,image->pixels);
    } else {
        sceGuTexMode(GU_PSM_8888,0,0,0);
        sceGuTexImage(0,CAVE_TEXTURE,CAVE_TEXTURE,CAVE_TEXTURE,cave_pixels[bank]);
    }
    sceGuTexFlush();
}
/* Two passes implement T_B*alpha + T_A*(1-alpha), with the same lighting.
 * The original second UV pair has no longitudinal color-dependent offset. */
static void cave_draw_batch(int count,const MdVertex *vertices,const MdVertex *second_uv,const CaveSlice *slice) {
    int style=cave_scene->style,texture=cave_scene->texture_style&&!cave_scene->black;
    sceGuDisable(GU_BLEND);sceGuDepthMask(0);
    if(texture)cave_bind(slice->texture_a);else sceGuDisable(GU_TEXTURE_2D);
    MdVertex *base=sceGuGetMemory(count*sizeof(*base));memcpy(base,vertices,count*sizeof(*base));
    for(int i=0;i<count;i++) {
        if(cave_scene->black)base[i].color=0xff000000;
        else if(style>=2 && style!=7)base[i].color=vertices[i-i%3].color;
    }
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,NULL,base);
    if(texture && cave_options.multitexture) {
        MdVertex *second=sceGuGetMemory(count*sizeof(*second));memcpy(second,base,count*sizeof(*second));
        for(int i=0;i<count;i++){second[i].u=second_uv[i].u;second[i].v=second_uv[i].v;}
        cave_bind(5+slice->texture_b);sceGuDepthMask(1);sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,NULL,second);
    }
    sceGuDisable(GU_BLEND);sceGuDepthMask(0);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);
}
static void cave_clear_target(unsigned color) {
    /* sceGuClear uses gu_draw_buffer.width/height (480x272 after GU init),
     * NOT the offscreen viewport set by sceGuDrawBufferList. Clear the full
     * 512x256 color/Z target explicitly, without changing display ownership.
     * GE CLEAR_MODE = 0xd3; normal rendering is restored immediately. */
    MdPlainVertex *v=sceGuGetMemory(2*sizeof(*v));
    v[0]=(MdPlainVertex){color,0,0,0};
    v[1]=(MdPlainVertex){color,MD_WIDTH,MD_HEIGHT,0};
    sceGuSendCommandi(0xd3,((GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT)<<8)|1);
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,v);
    sceGuSendCommandi(0xd3,0);
}
static void cave_draw(int width,int height) {
    if(!cave_texture_ready) {
        cave_texture(cave_pixels[0]);
        /* Independent periodic assets, not the original copyrighted images. */
        for(int bank=1;bank<7;bank++)for(int y=0;y<CAVE_TEXTURE;y++)for(int x=0;x<CAVE_TEXTURE;x++) {
            float a=x*(6.283185307f/CAVE_TEXTURE),b=y*(6.283185307f/CAVE_TEXTURE);
            unsigned shade=(unsigned)(135+55*sinf(a*(bank+1)+sinf(b*3))+30*cosf(b*(bank+2)-a));
            cave_pixels[bank][y*CAVE_TEXTURE+x]=0xff000000|shade|(shade<<8)|(shade<<16);
        }
        cave_texture_ready=1;
        sceKernelDcacheWritebackRange(cave_pixels,sizeof(cave_pixels));
    }
    md_target(MD_TEXTURE_BASE,MD_WIDTH,MD_WIDTH,MD_HEIGHT);
    sceGuDepthBuffer((void *)(uintptr_t)(MD_TEXTURE_BASE+MD_TEXTURE_BYTES),MD_WIDTH);
    sceGuDepthRange(65535,0);sceGuDepthMask(0);sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);
    sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_CLIP_PLANES);
    unsigned background=cave_background_color(cave_scene->background_rgb,cave_options.fog,cave_scene->black);
    if(cave_options.fog){
        float horizon=fmaxf(1,fminf(CAVE_AHEAD-2,cave_scene->next-cave_scene->motion.travel-2));
        sceGuEnable(GU_FOG);sceGuFog(0,cave_fog_end(cave_scene->paths.seed,cave_scene->motion.travel,horizon),background);
    }else sceGuDisable(GU_FOG);
    cave_clear_target(background);
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexImage(0,CAVE_TEXTURE,CAVE_TEXTURE,CAVE_TEXTURE,cave_pixels[0]);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexFlush();
    ScePspFMatrix4 identity={.x={1,0,0,0},.y={0,1,0,0},.z={0,0,1,0},.w={0,0,0,1}};
    ScePspFMatrix4 view;
    float view_values[16];cave_view(cave_scene,cave_scene->motion.travel,view_values);
    memcpy(&view,view_values,sizeof(view));
    float focal_x,focal_y;
    cave_projection(cave_scene->paths.seed,cave_scene->motion.travel,width,height,&focal_x,&focal_y);
    ScePspFMatrix4 projection={.x={focal_x,0,0,0},.y={0,focal_y,0,0},
        .z={0,0,-1.006689f,-1},.w={0,0,-.200669f,0}};
    sceGuSetMatrix(GU_PROJECTION,&projection);sceGuSetMatrix(GU_VIEW,&view);sceGuSetMatrix(GU_MODEL,&identity);
    int first=(int)floorf(cave_scene->motion.travel)-CAVE_HISTORY;
    CaveClip clip;cave_clip_init(&clip,view_values,projection.x.x,projection.y.y);
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
                unsigned cost=384+(at-begin)*sizeof(MdVertex)*4;
                if(budget+cost>MD_LIST_BYTES-65536)return;
                budget+=cost;
                cave_draw_batch(at-begin,slice->vertices+begin,slice->secondary+begin,slice);
            }
            begin=at+3;
            if(at==slice->count || (a&b&c))continue;
            MdVertex clipped[CAVE_CLIP_VERTICES],second_uv[CAVE_CLIP_VERTICES];
            int count=cave_clip_triangle(&clip,slice->vertices+at,clipped);
            if(count<=0)continue;
            if(cave_scene->style>=2 && cave_scene->style!=7)
                for(int k=0;k<count;k++)clipped[k].color=slice->vertices[at].color;
            if(cave_clip_triangle(&clip,slice->secondary+at,second_uv)!=count)continue;
            unsigned bytes=count*sizeof(MdVertex);
            /* Reserve command/presentation headroom even at pathological
             * geometry density. Scratch belongs to this GU list until sync. */
            if(budget+bytes*5+464>MD_LIST_BYTES-65536)return;
            budget+=bytes*5+464;
            unsigned char *memory=sceGuGetMemory(bytes+63);
            MdVertex *vertices=(MdVertex *)(((uintptr_t)memory+63)&~(uintptr_t)63);
            memcpy(vertices,clipped,bytes);
            cave_draw_batch(count,vertices,second_uv,slice);
        }
    }
    if(cave_scene->style>=2 && cave_scene->style<=5) {
        unsigned color=cave_effect_color(cave_scene->paths.seed,cave_scene->motion.travel,cave_scene->black,0,0);
        sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuDepthMask(1);
        for(int i=0;i<CAVE_SLICES;i++) {
            CaveSlice *slice=&cave_scene->slices[i];if(slice->index<first || !slice->count)continue;
            unsigned cost=slice->count*2*sizeof(MdVertex)+256;
            if(budget+cost>MD_LIST_BYTES-65536)break;
            budget+=cost;
            MdVertex *lines=sceGuGetMemory(slice->count*2*sizeof(*lines));int count=0;
            for(int j=0;j<slice->count;j+=3)for(int k=0;k<3;k++) {
                MdVertex a=slice->wire[j+k],b=slice->wire[j+(k+1)%3];
                a.color=b.color=color;
                if(cave_clip_line(&clip,&a,&b)){lines[count++]=a;lines[count++]=b;}
            }
            if(count)sceGuDrawArray(GU_LINES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,NULL,lines);
        }
    }
    if(cave_options.hair && cave_scene->style==7) {
        unsigned color=cave_effect_color(cave_scene->paths.seed,cave_scene->motion.travel,cave_scene->black,1,cave_options.transparent_hair);
        sceGuDisable(GU_TEXTURE_2D);sceGuDepthMask(1);
        if(cave_options.transparent_hair){sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);}
        else sceGuDisable(GU_BLEND);
        for(int i=0;i<CAVE_SLICES;i++) {
            CaveSlice *slice=&cave_scene->slices[i];if(slice->index<first)continue;
            unsigned cost=slice->hair_count*sizeof(MdVertex)+256;
            if(budget+cost>MD_LIST_BYTES-65536)break;
            budget+=cost;
            MdVertex *lines=sceGuGetMemory(slice->hair_count*sizeof(*lines));int count=0;
            for(int j=0;j<slice->hair_count;j+=2) {
                MdVertex a=slice->hair[j],b=slice->hair[j+1];
                a.color=b.color=color;
                if(cave_clip_line(&clip,&a,&b)){lines[count++]=a;lines[count++]=b;}
            }
            if(count)sceGuDrawArray(GU_LINES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,NULL,lines);
        }
    }
}
