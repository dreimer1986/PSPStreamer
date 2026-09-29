/* Included after the scene field/pose helpers. No allocation or file I/O. */
#include "cave_models_bounds.h"
static int cave_combat_clear(const CaveScene *s,const float a[3],const float b[3]) {
    float distance=0;for(int k=0;k<3;k++)distance+=(b[k]-a[k])*(b[k]-a[k]);
    int steps=(int)(sqrtf(distance)*4)+1;if(steps>80)return 0;
    for(int i=1;i<=steps;i++) {
        float t=(float)i/steps,x=a[0]+(b[0]-a[0])*t,y=a[1]+(b[1]-a[1])*t,z=a[2]+(b[2]-a[2])*t;
        if(fabsf(x)>5.5f || fabsf(y)>5.5f || z>=s->next-1 || z<s->motion.travel-2 || cave_density(s,x,y,z)<.008f)return 0;
    }
    return 1;
}
static float cave_segment_distance(const float a[3],const float b[3],const float p[3],float *fraction) {
    float d[3],q[3],length=0,t=0;
    for(int k=0;k<3;k++){d[k]=b[k]-a[k];q[k]=p[k]-a[k];length+=d[k]*d[k];t+=q[k]*d[k];}
    t=length>1e-9f?fmaxf(0,fminf(1,t/length)):0;
    *fraction=t;
    float result=0;for(int k=0;k<3;k++){float e=q[k]-t*d[k];result+=e*e;}return result;
}
static void cave_combat_emit(CaveCombat *c,const float p[3],const float direction[3],int enemy) {
    for(int i=0;i<CAVE_BOLTS;i++)if(c->bolts[i].life<=0) {
        CaveBolt *b=&c->bolts[i];b->life=2;b->enemy=enemy;
        for(int k=0;k<3;k++){b->p[k]=p[k];b->v[k]=direction[k]*(enemy?10:22);}
        return;
    }
}
static void cave_combat_spawn(CaveScene *s) {
    CaveCombat *c=&s->combat;
    if(c->health>0 || s->ready<10)return;
    int model=random_step(&c->random)%CAVE_SHIPS;
    float radius[3];for(int k=0;k<3;k++)radius[k]=.65f*cave_model_half[model][k]+.06f;
    /* Curved floors need not be flat across a wide footprint. Fit the hull
     * plus a little passing room; a stationary ship may hover above a slope. */
    for(int depth=0;depth<3;depth++) {
        float z=s->motion.travel+10+((depth==1)?-2:(depth==2)?2:0);
        if(z>=s->next-2)continue;
        float x,y;cave_camera(s,z,&x,&y);
        for(int attempt=0;attempt<6;attempt++) {
            float px=x+(attempt%3==1?.55f:attempt%3==2?-.55f:0),py=y;
            if(cave_density(s,px,py,z)<.008f)continue;
            if(attempt<3) {
                while(py>-5 && cave_density(s,px,py-.1f,z)>.008f)py-=.1f;
                py+=radius[1]+.06f;
            } else py-=.25f;
            if(fabsf(px)+radius[0]>5.5f || fabsf(py)+radius[1]>5.5f)continue;
            int clear=cave_density(s,px,py,z)>.008f;
            for(int k=0;k<6 && clear;k++) {
                float p[3]={px,py,z};p[k/2]+=radius[k/2]*(k&1?1:-1);
                clear=cave_density(s,p[0],p[1],p[2])>.008f;
            }
            if(!clear || (cave_density(s,px-.6f,py+.25f,z)<.008f &&
                          cave_density(s,px+.6f,py+.25f,z)<.008f))continue;
            c->health=100;c->model=model;
            c->enemy[0]=px;c->enemy[1]=py;c->enemy[2]=z;
            c->aim[0]=0;c->aim[1]=0;c->aim[2]=-1;c->visible=0;
            c->enemy_cooldown=CAVE_ENEMY_FIRE_SECONDS;return;
        }
    }
}
static void cave_combat_step(CaveScene *s,float dt) {
    CaveCombat *c=&s->combat;
    if(!s->flight || s->game.phase!=CAVE_GAME_ALIVE || s->game.paused || dt<=0)return;
    c->spawn-=dt;c->spawn_retry=fmaxf(0,c->spawn_retry-dt);c->flash=fmaxf(0,c->flash-dt);
    if(c->health>0 && c->enemy[2]<s->motion.travel-1)c->health=0;
    if(c->spawn<=0){c->pending_spawn=1;c->spawn=0;}
    if(c->pending_spawn && !c->health && !c->spawn_retry) {
        cave_combat_spawn(s);c->spawn_retry=1;
        if(c->health>0){c->pending_spawn=0;c->spawn=15+(random_step(&c->random)%5001)*.001f;}
    }
    float player[3]={s->flight_x,s->flight_y,s->motion.travel+2};
    c->fire_cooldown=fmaxf(0,c->fire_cooldown-dt);
    if(c->fire && !c->fire_cooldown && s->flight_age>.85f) {
        float dx,dy;cave_flight_direction(s,&dx,&dy);float dir[3]={dx,dy,1};unit3(dir);
        float muzzle[3];for(int k=0;k<3;k++)muzzle[k]=player[k]+dir[k]*.3f;
        cave_combat_emit(c,muzzle,dir,0);c->fire_cooldown=CAVE_PLAYER_FIRE_SECONDS;
    }
    if(c->health>0) {
        int visible=cave_combat_clear(s,c->enemy,player);
        c->visible=visible?c->visible+dt:0;
        if(c->visible>.55f) {
            float dir[3];for(int k=0;k<3;k++)dir[k]=player[k]-c->enemy[k];unit3(dir);
            float turn=1-expf(-dt*3);
            for(int k=0;k<3;k++)c->aim[k]+=(dir[k]-c->aim[k])*turn;
            unit3(c->aim);c->enemy_cooldown-=dt;
            if(c->enemy_cooldown<=0 && dot3(c->aim,dir)>.97f) {
                float muzzle[3];for(int k=0;k<3;k++)muzzle[k]=c->enemy[k]+c->aim[k]*.4f;
                cave_combat_emit(c,muzzle,c->aim,1);c->enemy_cooldown=CAVE_ENEMY_FIRE_SECONDS;
            }
        }
    }
    for(int i=0;i<CAVE_BOLTS;i++) {
        CaveBolt *b=&c->bolts[i];if(b->life<=0)continue;
        float next[3];for(int k=0;k<3;k++)next[k]=b->p[k]+b->v[k]*dt;
        b->life-=dt;
        float fraction,endpoint[3];
        const float *target=b->enemy?player:c->enemy;
        int hit=(b->enemy || c->health>0) &&
            cave_segment_distance(b->p,next,target,&fraction)<(b->enemy?.055f:.10f);
        /* Stop the visibility test at the target, not at a wall behind it. */
        for(int k=0;k<3;k++)endpoint[k]=hit?b->p[k]+(next[k]-b->p[k])*fraction:next[k];
        if(!cave_combat_clear(s,b->p,endpoint)){b->life=0;continue;}
        if(hit && b->enemy) {
            if(cave_game_blaster_hit(&s->game,s->flight_barrel!=0))s->flight_impact=.2f;
            b->life=0;
        } else if(hit) {
            c->health-=CAVE_BLASTER_DAMAGE;c->flash=.18f;b->life=0;
            if(c->health<=0)cave_game_enemy_destroyed(&s->game,100);
        }
        memcpy(b->p,next,sizeof(next));
    }
}
