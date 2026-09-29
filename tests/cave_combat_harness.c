/* Analytic open/blocked room: isolate combat rules from random tunnel shape. */
#include "cave_visual.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int blocked;
float cave_density(const CaveScene *s,float x,float y,float z) {
    (void)s;(void)x;(void)z;return blocked?-1:y;
}
void cave_camera(const CaveScene *s,float z,float *x,float *y) {
    (void)s;(void)z;*x=0;*y=2;
}
static unsigned random_step(unsigned *r){*r=*r*1664525U+1013904223U;return *r;}
static float dot3(const float a[3],const float b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void unit3(float a[3]){float n=sqrtf(dot3(a,a));if(n>1e-8f)for(int i=0;i<3;i++)a[i]/=n;}
static void cave_flight_direction(const CaveScene *s,float *x,float *y){(void)s;*x=*y=0;}
#include "cave_combat_impl.h"
static void reset(CaveScene *s) {
    memset(s,0,sizeof(*s));s->flight=1;s->flight_age=1;s->flight_y=2;
    s->next=32;s->ready=16;s->combat.spawn=18;s->combat.random=123;
    cave_game_start(&s->game);s->game.protection=0;blocked=0;
}
static int bolts(CaveScene *s,int enemy) {
    int n=0;for(int i=0;i<CAVE_BOLTS;i++)if(s->combat.bolts[i].life>0 && s->combat.bolts[i].enemy==enemy)n++;
    return n;
}
int main(void) {
    CaveScene *s=aligned_alloc(64,sizeof(*s));assert(s);reset(s);
    s->combat.fire=1;cave_combat_step(s,.01f);assert(bolts(s,0)==1);
    cave_combat_step(s,.1f);assert(bolts(s,0)==1);
    cave_combat_step(s,.39f);assert(bolts(s,0)==1);
    cave_combat_step(s,.011f);assert(bolts(s,0)==2);
    s->game.paused=1;CaveCombat saved=s->combat;cave_combat_step(s,1);assert(!memcmp(&saved,&s->combat,sizeof(saved)));
    reset(s);blocked=1;s->combat.spawn=0;cave_combat_step(s,.01f);
    assert(!s->combat.health && s->combat.pending_spawn==1);
    for(int i=0;i<500;i++)cave_combat_step(s,.1f);
    assert(s->combat.pending_spawn==1 && !s->combat.health); /* bounded backlog */
    blocked=0;s->combat.spawn_retry=1;cave_combat_step(s,.1f);assert(!s->combat.health);
    for(int i=0;i<11 && !s->combat.health;i++)cave_combat_step(s,.1f);
    assert(s->combat.health==100 && s->combat.model>=0 && s->combat.model<CAVE_SHIPS);
    assert(!s->combat.pending_spawn && s->combat.spawn>=15 && s->combat.spawn<=20);
    assert(s->combat.enemy[1]>0 && s->combat.enemy[1]<.5f);
    assert(!bolts(s,1));for(int i=0;i<4;i++)cave_combat_step(s,.1f);assert(!bolts(s,1));
    for(int i=0;i<20 && !bolts(s,1);i++)cave_combat_step(s,.1f);
    assert(bolts(s,1));
    reset(s);s->combat.health=100;s->combat.enemy[1]=2;s->combat.enemy[2]=4;
    const float muzzle[3]={0,2,3},dir[3]={0,0,1};
    for(int i=0;i<10;i++){cave_combat_emit(&s->combat,muzzle,dir,0);cave_combat_step(s,.05f);}
    assert(!s->combat.health && s->game.kills==1 && s->game.bonus==100);
    cave_combat_step(s,.05f);assert(s->game.kills==1);
    reset(s);const float incoming[3]={0,2,2.5f},toward[3]={0,0,-1};
    cave_combat_emit(&s->combat,incoming,toward,1);cave_combat_step(s,.05f);assert(s->game.health==90);
    s->flight_barrel=1;cave_combat_emit(&s->combat,incoming,toward,1);cave_combat_step(s,.05f);assert(s->game.health==90);
    s->flight_barrel=0;blocked=1;cave_combat_emit(&s->combat,incoming,toward,1);cave_combat_step(s,.05f);assert(s->game.health==90);
    reset(s);for(int i=0;i<100;i++)cave_combat_emit(&s->combat,muzzle,dir,0);assert(bolts(s,0)==CAVE_BOLTS);
    free(s);puts("Combat: bounded pool/backlog, fire rate, pause, deferred spawn, acquisition, damage, kill reward, wall/roll protection OK");
}
