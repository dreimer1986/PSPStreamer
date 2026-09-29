/* Bounded enemy and yellow pulse geometry, using the existing depth buffer. */
#define CAVE_COMBAT_FORMAT (GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D)
static void cave_combat_unit(float p[3]) {
    float n=sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if(n>1e-8f)for(int k=0;k<3;k++)p[k]/=n;
}
static void cave_combat_cross(const float a[3],const float b[3],float out[3]) {
    for(int k=0;k<3;k++)out[k]=a[(k+1)%3]*b[(k+2)%3]-a[(k+2)%3]*b[(k+1)%3];
}
static void cave_draw_combat(void) {
    if(!cave_scene || cave_scene->game.phase!=CAVE_GAME_ALIVE)return;
    CaveCombat *c=&cave_scene->combat;
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_FOG);sceGuDisable(GU_BLEND);sceGuDepthMask(0);
    if(c->health>0) {
        float center[3],q[3],forward[3],up[3],right[3];
        cave_world_point(cave_scene,c->enemy[0],c->enemy[1],c->enemy[2],center);
        cave_world_point(cave_scene,c->enemy[0]+c->aim[0]*.1f,c->enemy[1]+c->aim[1]*.1f,c->enemy[2]+c->aim[2]*.1f,q);
        for(int k=0;k<3;k++)forward[k]=q[k]-center[k];
        cave_combat_unit(forward);
        cave_world_point(cave_scene,c->enemy[0],c->enemy[1]+.1f,c->enemy[2],q);
        for(int k=0;k<3;k++)up[k]=q[k]-center[k];
        cave_combat_unit(up);
        cave_combat_cross(forward,up,right);cave_combat_unit(right);cave_combat_cross(right,forward,up);
        enum {CAPACITY=61440/sizeof(MdVertex)};
        MdVertex *out=sceGuGetMemory(CAPACITY*sizeof(*out));int used=0;
        const CaveModel *model=&cave_models[c->model];
        for(int i=0;i<model->count;i+=3) {
            MdVertex tri[3],clipped[CAVE_CLIP_VERTICES];
            for(int j=0;j<3;j++) {
                tri[j]=model->vertices[i+j];float p[3];
                for(int k=0;k<3;k++)p[k]=center[k]+CAVE_ENEMY_SCALE*(right[k]*tri[j].x+up[k]*tri[j].y-forward[k]*tri[j].z);
                tri[j].x=p[0];tri[j].y=p[1];tri[j].z=p[2];
                if(c->flash>0)tri[j].color=0xffaaffff;
            }
            int n=cave_clip_triangle(&cave_frame_clip,tri,clipped);if(used+n>CAPACITY)break;
            memcpy(out+used,clipped,n*sizeof(*out));used+=n;
        }
        if(used)sceGuDrawArray(GU_TRIANGLES,CAVE_COMBAT_FORMAT,used,NULL,out);
    }
    float ship[3],right[3],up[3],forward[3];cave_ship_pose(cave_scene,ship,right,up,forward);
    /* Pulse silhouettes have pointed ends, a yellow body and narrow bright
     * core. They are discrete moving bolts, not an instantaneous beam. */
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);sceGuDepthMask(1);
    for(int i=0;i<CAVE_BOLTS;i++)if(c->bolts[i].life>0) {
        CaveBolt *b=&c->bolts[i];float a[3],z[3];
        cave_world_point(cave_scene,b->p[0],b->p[1],b->p[2],a);
        cave_world_point(cave_scene,b->p[0]-b->v[0]*.025f,b->p[1]-b->v[1]*.025f,b->p[2]-b->v[2]*.025f,z);
        for(int pass=0;pass<2;pass++) {
            float width=(pass?.016f:.055f)*(1+.15f*sinf(cave_scene->flight_age*35+i));
            MdVertex points[4];unsigned colour=pass?0xffcfffff:0xc020dfff;
            for(int j=0;j<4;j++) {
                float p[3];for(int k=0;k<3;k++)p[k]=j==0?a[k]:j==2?z[k]:(a[k]+z[k])*.5f+right[k]*width*(j==1?1:-1);
                points[j]=(MdVertex){0,0,colour,p[0],p[1],p[2]};
            }
            for(int j=0;j<2;j++) {
                MdVertex tri[3]={points[0],points[j+1],points[j+2]},clipped[CAVE_CLIP_VERTICES];
                int n=cave_clip_triangle(&cave_frame_clip,tri,clipped);if(!n)continue;
                MdVertex *out=sceGuGetMemory(n*sizeof(*out));memcpy(out,clipped,n*sizeof(*out));
                sceGuDrawArray(GU_TRIANGLES,CAVE_COMBAT_FORMAT,n,NULL,out);
            }
        }
    }
    sceGuDepthMask(0);sceGuDisable(GU_BLEND);
}
