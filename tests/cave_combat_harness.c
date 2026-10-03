/* Analytic open/blocked room: isolate combat rules from random tunnel shape. */
#include "cave_visual.h"
#include "cave_rumble.h"
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
    assert(s->combat.health==CAVE_ENEMY_MAX_HEALTH && s->combat.drone && s->combat.model!=s->ship_model);
    assert(!s->combat.pending_spawn && s->combat.spawn>=15 && s->combat.spawn<=20);
    assert(s->combat.enemy[1]>0 && s->combat.enemy[1]<.7f);
    assert(!bolts(s,1));for(int i=0;i<4;i++)cave_combat_step(s,.1f);assert(!bolts(s,1));
    for(int i=0;i<20 && !bolts(s,1);i++)cave_combat_step(s,.1f);
    assert(bolts(s,1));
    reset(s);s->combat.health=CAVE_ENEMY_MAX_HEALTH;s->combat.enemy[1]=2;s->combat.enemy[2]=4;
    const float muzzle[3]={0,2,3},dir[3]={0,0,1};
    for(int i=0;i<3;i++) {
        cave_combat_emit(&s->combat,muzzle,dir,0);cave_combat_step(s,.05f);
        assert(s->combat.health==(2-i)*CAVE_ENEMY_SHOT_DAMAGE && s->game.kills==(i==2?1U:0U));
    }
    assert(!s->combat.health && s->game.kills==1 && s->game.bonus==100);
    assert(s->combat.explosion_active && s->combat.explosion_age==0);
    assert(s->combat.explosion[1]==2 && s->combat.explosion[2]==4);
    cave_combat_step(s,.05f);assert(s->game.kills==1);
    reset(s);const float incoming[3]={0,2,2.5f},toward[3]={0,0,-1};
    cave_combat_emit(&s->combat,incoming,toward,1);cave_combat_step(s,.05f);assert(s->game.health==90);
    s->flight_barrel=1;cave_combat_emit(&s->combat,incoming,toward,1);cave_combat_step(s,.05f);assert(s->game.health==90);
    s->flight_barrel=0;blocked=1;cave_combat_emit(&s->combat,incoming,toward,1);cave_combat_step(s,.05f);assert(s->game.health==90);
    reset(s);for(int i=0;i<100;i++)cave_combat_emit(&s->combat,muzzle,dir,0);assert(bolts(s,0)==CAVE_BOLTS);
    int drones=0,turrets=0;
    for(unsigned seed=1;seed<100;seed++) {
        reset(s);s->ship_model=seed%CAVE_SHIPS;s->combat.random=seed;cave_combat_spawn(s);
        assert(s->combat.health==CAVE_ENEMY_MAX_HEALTH);
        if(s->combat.drone){drones++;assert(s->combat.model!=s->ship_model && s->combat.model<CAVE_SHIPS);}
        else {turrets++;assert(s->combat.model==CAVE_ENEMY_MODEL);}
        s->combat.health=0;cave_combat_spawn(s);
        assert(s->combat.health==CAVE_ENEMY_MAX_HEALTH && !s->combat.drone && s->combat.model==CAVE_ENEMY_MODEL);turrets++;
    }
    assert(drones && turrets);
    reset(s);s->combat.drone=1;s->combat.model=1;s->combat.health=3;
    s->combat.enemy[1]=2;s->combat.enemy[2]=10;
    memcpy(s->combat.waypoint,s->combat.enemy,sizeof(s->combat.enemy));
    s->combat.waypoint[0]=.5f;s->combat.waypoint_wait=1;
    cave_drone_step(s,.1f);assert(s->combat.enemy[0]>0 && s->combat.enemy[0]<.06f);
    blocked=1;float x=s->combat.enemy[0];cave_drone_step(s,.1f);assert(s->combat.enemy[0]==x);
    reset(s);s->combat.shield_timer=0;cave_combat_step(s,.01f);
    assert(s->combat.shield_active && s->combat.shield[0]==0 && s->combat.shield[1]==2);
    /* Swept collection catches passing the plane between frames, only once. */
    s->game.health=20;s->combat.shield[2]=3;s->motion.travel=2;
    cave_combat_step(s,.1f);assert(s->game.health==86 && !s->combat.shield_active);
    cave_combat_step(s,.1f);assert(s->game.health==86);
    s->combat.shield_active=1;s->combat.shield[2]=4;
    cave_combat_step(s,.1f);assert(s->game.health==100 && !s->combat.shield_active);
    reset(s);blocked=1;cave_combat_step(s,.1f);assert(!s->combat.shield_active);
    reset(s);s->combat.shield_active=1;s->combat.shield[0]=4;s->combat.shield[1]=2;s->combat.shield[2]=3;
    s->game.health=20;s->motion.travel=2;cave_combat_step(s,.1f);assert(s->game.health==20);
    s->motion.travel=5;cave_combat_step(s,.1f);assert(!s->combat.shield_active && s->combat.shield_timer==35);
    reset(s);s->combat.explosion_active=1;s->combat.explosion_age=2.49f;
    cave_combat_step(s,.02f);assert(!s->combat.explosion_active);
    reset(s);s->combat.health=CAVE_ENEMY_MAX_HEALTH;s->combat.enemy[1]=2;s->combat.enemy[2]=2;
    float player[3]={0,2,2};cave_enemy_contact(s,player);
    assert(s->game.health==70 && s->combat.health==6 && s->rumble_event==1);
    s->game.protection=0;cave_enemy_contact(s,player);assert(s->game.health==70 && s->combat.health==6);
    player[0]=3;cave_enemy_contact(s,player);player[0]=0;cave_enemy_contact(s,player);
    assert(s->game.health==40 && s->combat.health==0 && s->game.kills==1 && s->combat.explosion_active);
    reset(s);s->combat.health=15;s->combat.enemy[1]=2;s->combat.enemy[2]=4;
    s->combat.previous_player_valid=1;s->combat.previous_player[1]=2;s->combat.previous_player[2]=2;
    player[2]=6;cave_enemy_contact(s,player);assert(s->game.health==70 && s->combat.health==6);
    reset(s);s->combat.health=15;s->combat.enemy[1]=2;s->combat.enemy[2]=2;
    player[2]=2;s->game.health=20;cave_enemy_contact(s,player);assert(!s->game.health && s->game.phase==CAVE_GAME_EXPLODING);
    unsigned small,large;
    cave_rumble_mix(1,1,1,100,100,0,&small,&large);assert(!small&&!large);
    cave_rumble_mix(1,1,1,0,0,1,&small,&large);assert(!small&&!large);
    cave_rumble_mix(0,0,1,0,50,1,&small,&large);assert(!small&&large==128);
    cave_rumble_mix(0,0,1,0,100,1,&small,&large);assert(small&&large==255);
    cave_rumble_mix(0,0,.12f,0,100,1,&small,&large);assert(!small&&large<40);
    cave_rumble_mix(.5f,.2f,0,50,0,1,&small,&large);assert(!small&&large>0&&large<128);
    free(s);puts("Combat: damage, contact latch/sweep/death, rumble gains, spawn and projectile rules OK");
}
