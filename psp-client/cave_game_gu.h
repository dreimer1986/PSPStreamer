/* Game-only overlay. Normal Monkey rendering never allocates this atlas. */
#include "cave_scores.h"
#include "cave_rumble.h"
void md_cave_rumble(int playing,unsigned *small,unsigned *large) {
    *small=*large=0;if(!cave_scene)return;
    CaveScene *s=cave_scene;
    cave_rumble_mix(s->motion.bass,s->motion.pulse,
        s->flight&&(s->game.phase==CAVE_GAME_ALIVE||s->game.phase==CAVE_GAME_EXPLODING)?s->rumble_event:0,
        cave_options.rumble_music,cave_options.rumble_game,playing&&!s->game.paused,small,large);
}
#include <stdlib.h>
static const unsigned char *cave_hud_font;
static unsigned short *cave_hud_atlas;
extern const unsigned short monkey_flight_logo[];
static const char *cave_hud_labels[11]={"SHIELD","Score","Hall of Fame","X / O: return","Save failed","GAME OVER","Game Start","Exit","L + R: exit","No scores yet","UP / DOWN   X: OK   O: Exit"};
static CaveHall cave_hall;
static int cave_hall_loaded,cave_hall_rank=-1,cave_hall_save_failed;
#define CAVE_SCORES_A "ms0:/PSP/SYSTEM/PSPStreamer-flight-a.dat"
#define CAVE_SCORES_B "ms0:/PSP/SYSTEM/PSPStreamer-flight-b.dat"
void md_cave_game_ui(const unsigned char *font,int paused,const char *const labels[11]) {
    cave_hud_font=font;
    if(labels)for(int i=0;i<11;i++)cave_hud_labels[i]=labels[i];
    if(cave_scene) {
        if(cave_scene->game.paused!=!!paused)cave_scene->motion.previous=0;
        cave_scene->game.paused=!!paused;
    }
}
int md_cave_game_menu(int move,int ship_move,int confirm,int back) {
    if(!cave_scene)return 0;
    CaveGame *g=&cave_scene->game;
    if(g->phase==CAVE_GAME_HALL) {
        if(confirm || back){g->phase=g->completed?CAVE_GAME_OFF:CAVE_GAME_INTRO;cave_scene->flight=0;}
        return 1;
    }
    if(g->phase==CAVE_GAME_EXPLODING)return 1;
    if(g->phase==CAVE_GAME_ALIVE)return 1;
    if(g->phase!=CAVE_GAME_INTRO)return 0;
    if(ship_move)cave_scene->ship_model=(cave_scene->ship_model+(ship_move>0?1:CAVE_SHIPS-1))%CAVE_SHIPS;
    if(move)g->selection=(g->selection+(move>0?1:2))%3;
    if(back || (confirm && g->selection==2)){g->phase=CAVE_GAME_OFF;cave_scene->flight=0;}
    else if(confirm && g->selection==0) {
        int paused=g->paused;
        for(int k=0;k<3;k++)cave_scene->ship_half[k]=cave_models[cave_scene->ship_model].half[k]*CAVE_SHIP_SCALE+CAVE_SHIP_SKIN;
        cave_flight_input(cave_scene,1,128,128,0,0);g->paused=paused;
        cave_scene->motion.previous=0;
    } else if(confirm && g->selection==1){g->phase=CAVE_GAME_HALL;g->completed=0;}
    return 1;
}
/* Outside a GU list: storage is touched once per completed flight, not per
 * frame. Persistence is deliberately independent of renderer teardown. */
static void cave_game_prepare_ui(void) {
    if(!cave_scene || cave_scene->game.phase==CAVE_GAME_OFF)return;
    if(!cave_hall_loaded) {
        cave_scores_load(&cave_hall,CAVE_SCORES_A,CAVE_SCORES_B);cave_hall_loaded=1;
    }
    CaveGame *g=&cave_scene->game;
    if(g->completed && !g->recorded) {
        g->recorded=1;cave_hall_save_failed=0;
        cave_hall_rank=cave_hall_insert(cave_hall.rows,&cave_hall.count,cave_game_score(g));
        if(cave_hall_rank>=0 && !cave_scores_save(&cave_hall,CAVE_SCORES_A,CAVE_SCORES_B)) {
            cave_hall_save_failed=1;md_trace("Flight Hall of Fame save failed");
        }
    }
    if(!cave_hud_atlas && cave_hud_font) {
        cave_hud_atlas=memalign(64,256*128*2);if(!cave_hud_atlas)return;
        memset(cave_hud_atlas,0,256*128*2);
        for(int c=32;c<128;c++)for(int y=0;y<20;y++)for(int x=0;x<16;x++) {
            int gx=((c-32)&15)*16,gy=((c-32)>>4)*20;
            unsigned alpha=cave_hud_font[(c>>4)*20*256+(c&15)*16+y*256+x]>>4;
            cave_hud_atlas[(gy+y)*256+gx+x]=(alpha<<12)|0xfff;
        }
        sceKernelDcacheWritebackRange(cave_hud_atlas,256*128*2);
    }
}
static void cave_hud_rect(float x,float y,float w,float h,unsigned color) {
    sceGuDisable(GU_TEXTURE_2D);
    MdPlainVertex *v=sceGuGetMemory(2*sizeof(*v));
    v[0]=(MdPlainVertex){color,x,y,0};v[1]=(MdPlainVertex){color,x+w,y+h,0};
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,v);
}
static void cave_hud_text(float x,float y,float scale,const char *text,unsigned color) {
    if(!cave_hud_atlas)return;
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);
    sceGuTexImage(0,256,128,256,cave_hud_atlas);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexFlush();
    for(int i=0;text[i] && i<48;i++) {
        unsigned c=(unsigned char)text[i];if(c<32 || c>=128)c='?';
        float u=((c-32)&15)*16,v=((c-32)>>4)*20;
        MdVertex *q=sceGuGetMemory(2*sizeof(*q));
        q[0]=(MdVertex){u,v,color,x,y,0};q[1]=(MdVertex){u+16,v+20,color,x+16*scale,y+20*scale,0};
        sceGuDrawArray(GU_SPRITES,MD_FORMAT,2,NULL,q);x+=14*scale;
    }
}
static void cave_hud_logo(float left,float top,float width) {
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_5650,0,0,0);
    sceGuTexImage(0,256,128,256,monkey_flight_logo);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexFlush();
    MdVertex *v=sceGuGetMemory(2*sizeof(*v));
    v[0]=(MdVertex){0,0,0xffffffff,left,top,0};v[1]=(MdVertex){256,85,0xffffffff,left+width,top+width*85/256,0};
    sceGuDrawArray(GU_SPRITES,MD_FORMAT,2,NULL,v);
}
/* Selection-only orthographic preview. Cache the painter-sorted geometry;
 * no extra render target, texture, framebuffer switch or per-frame sorting. */
typedef struct {float depth;int index;} CavePreviewFace;
static int cave_preview_order(const void *a,const void *b) {
    float d=((const CavePreviewFace *)a)->depth-((const CavePreviewFace *)b)->depth;
    return d<0?-1:d>0;
}
static void cave_hud_ship(float cx,float cy,float size) {
    static int cached=-1,count;
    static MdVertex projected[1800];
    static CavePreviewFace order[600];
    int selected=cave_scene->ship_model;
    const CaveModel *model=&cave_models[selected];
    if(selected!=cached) {
        count=model->count;
        for(int i=0;i<count;i++) {
            MdVertex v=model->vertices[i];
            float x=.8660254f*v.x+.5f*v.z,z=-.5f*v.x+.8660254f*v.z;
            projected[i]=v;projected[i].x=x;
            projected[i].y=-(.8660254f*v.y-.5f*z);
            projected[i].z=.5f*v.y+.8660254f*z;
        }
        for(int i=0;i<count/3;i++)order[i]=(CavePreviewFace){
            (projected[3*i].z+projected[3*i+1].z+projected[3*i+2].z)/3,i};
        qsort(order,count/3,sizeof(*order),cave_preview_order);cached=selected;
    }
    MdVertex *out=sceGuGetMemory(count*sizeof(*out));
    for(int i=0;i<count;i++) {
        out[i]=projected[order[i/3].index*3+i%3];
        out[i].x=cx+size*out[i].x;out[i].y=cy+size*out[i].y;out[i].z=0;
    }
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLES,MD_FORMAT,count,NULL,out);
}
/* Native scanout overlay, clipped to the visualization rectangle. Text is
 * not baked into a low-resolution tunnel texture or accumulated as feedback. */
static void cave_game_draw_hud(int left,int top,int width,int height) {
    if(!cave_scene || cave_scene->game.phase==CAVE_GAME_OFF)return;
    CaveGame *g=&cave_scene->game;CaveScore score=cave_game_score(g);
    float scale=fminf(width/480.f,height/272.f),x=left+8*scale,y=top+8*scale;
    char text[64];
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_FOG);sceGuDepthMask(1);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    if(g->phase==CAVE_GAME_INTRO) {
        cave_hud_rect(left+width*.05f,top+height*.04f,width*.9f,height*.92f,0xee000000);
        cave_hud_logo(left+width*.10f,top+height*.08f,width*.50f);
        cave_hud_ship(left+width*.78f,top+height*.24f,fminf(width*.23f,height*.30f));
        snprintf(text,sizeof(text),"<  Low Poly %d / %d  >",cave_scene->ship_model+1,CAVE_SHIPS);
        cave_hud_text(left+width*.24f,top+height*.79f,.65f*scale,text,0xff40eaff);
        for(int i=0;i<3;i++) {
            float row=top+height*.46f+i*27*scale;
            if(i==g->selection)cave_hud_rect(left+width*.23f,row-2*scale,width*.54f,25*scale,0xff304a20);
            snprintf(text,sizeof(text),"%s %s",i==g->selection?">":" ",cave_hud_labels[i==0?6:i==1?2:7]);
            cave_hud_text(left+width*.27f,row,.9f*scale,text,0xffffffff);
        }
        cave_hud_text(left+width*.2f,top+height*.86f,.6f*scale,cave_hud_labels[10],0xffc0c0c0);
    } else if(g->phase==CAVE_GAME_ALIVE) {
        cave_hud_rect(x,y,170*scale,20*scale,0xb0000000);
        snprintf(text,sizeof(text),"%s %u",cave_hud_labels[1],(unsigned)score.points);
        cave_hud_text(x+4*scale,y+2*scale,.65f*scale,text,0xffffffff);
        /* Star Fox SNES-inspired shield: blue-shadowed white label, thin
         * pale frame, dark empty track and solid red fill, bottom left. */
        float sy=top+height-38*scale;
        cave_hud_text(x+2*scale,sy+scale,.7f*scale,cave_hud_labels[0],0xffd06020);
        cave_hud_text(x+scale,sy,.7f*scale,cave_hud_labels[0],0xffffffff);
        cave_hud_rect(x,sy+17*scale,118*scale,14*scale,0xffd8e8d8);
        cave_hud_rect(x+2*scale,sy+19*scale,114*scale,10*scale,0xff284e24);
        if(g->health)cave_hud_rect(x+3*scale,sy+20*scale,112*g->health*.01f*scale,8*scale,0xff1828f8);
    } else {
        cave_hud_rect(left+width*.05f,top+height*.04f,width*.9f,height*.92f,g->phase==CAVE_GAME_HALL?0xe8000000:0x60000000);
        float tx=left+width*.10f,ty=top+height*.07f;
        cave_hud_text(tx,ty,.85f*scale,cave_hud_labels[g->phase==CAVE_GAME_HALL?2:5],0xff60c0ff);
        if(g->completed) {
            snprintf(text,sizeof(text),"%s %u   %us",cave_hud_labels[1],(unsigned)score.points,(unsigned)score.seconds);
            cave_hud_text(tx,ty+23*scale,.65f*scale,text,0xffffffff);
        }
        if(g->phase==CAVE_GAME_HALL) {
            for(int i=0;i<cave_hall.count;i++) {
                snprintf(text,sizeof(text),"%2d. %7u    %5us",i+1,(unsigned)cave_hall.rows[i].points,(unsigned)cave_hall.rows[i].seconds);
                cave_hud_text(tx,ty+(46+i*15)*scale,.65f*scale,text,g->completed && i==cave_hall_rank?0xff60e0ff:0xffeeeeee);
            }
            if(!cave_hall.count)cave_hud_text(tx,ty+50*scale,.65f*scale,cave_hud_labels[9],0xffffffff);
            cave_hud_text(tx,ty+199*scale,.6f*scale,cave_hud_labels[3],0xffffffff);
            if(cave_hall_save_failed)cave_hud_text(tx,ty+216*scale,.55f*scale,cave_hud_labels[4],0xff6060ff);
        }
    }
    if(g->exit_hold>0) {
        snprintf(text,sizeof(text),"%s %.1f / 5s",cave_hud_labels[8],g->exit_hold);
        cave_hud_rect(left+width*.08f,top+height-65*scale,width*.84f,20*scale,0xe8000000);
        cave_hud_text(left+width*.1f,top+height-64*scale,.65f*scale,text,0xff60e0ff);
    }
    sceGuDisable(GU_BLEND);sceGuTexMode(md_pixel_format,0,0,0);
}
static void cave_draw_explosion_at(const float center[3],const float right[3],const float up[3],const float forward[3],float t) {
    if(t<0 || t>=2.5f)return;
    MdVertex *v=sceGuGetMemory(48*CAVE_CLIP_VERTICES*sizeof(*v));int used=0;
    unsigned alpha=(unsigned)(255*(1-t/2.5f));
    for(int i=0;i<48;i++) {
        float a=i*2.39996323f,z=(i+.5f)/24.f-1,r=sqrtf(1-z*z),distance=.03f+t*(.25f+(i%7)*.12f);
        float p[3],size=.025f+.055f*(1-t/2.5f);
        for(int k=0;k<3;k++)p[k]=center[k]+distance*(right[k]*cosf(a)*r+up[k]*sinf(a)*r+forward[k]*z)-up[k]*t*t*.12f;
        MdVertex tri[3],clipped[CAVE_CLIP_VERTICES];
        for(int j=0;j<3;j++) {
            float dx=j==0?-size:j==1?size:0,dy=j==2?size:-size;
            tri[j]=(MdVertex){0,0,(alpha<<24)|((i%3)?0x002080ff:0x0080ffff),p[0]+right[0]*dx+up[0]*dy,p[1]+right[1]*dx+up[1]*dy,p[2]+right[2]*dx+up[2]*dy};
        }
        int count=cave_clip_triangle(&cave_frame_clip,tri,clipped);memcpy(v+used,clipped,count*sizeof(*v));used+=count;
    }
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_FOG);sceGuDepthMask(1);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);
    if(used)sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,used,NULL,v);
    sceGuDisable(GU_BLEND);
}
static void cave_game_draw_explosion(void) {
    if(!cave_scene)return;
    float center[3],right[3],up[3],forward[3];
    if(cave_scene->game.phase==CAVE_GAME_EXPLODING) {
        cave_ship_pose(cave_scene,center,right,up,forward);
        cave_draw_explosion_at(center,right,up,forward,(float)cave_scene->game.death_seconds);
    } else if(cave_scene->game.phase==CAVE_GAME_ALIVE && cave_scene->combat.explosion_active) {
        CaveCombat *c=&cave_scene->combat;
        cave_ship_pose(cave_scene,center,right,up,forward);
        cave_world_point(cave_scene,c->explosion[0],c->explosion[1],c->explosion[2],center);
        cave_draw_explosion_at(center,right,up,forward,c->explosion_age);
    }
}
