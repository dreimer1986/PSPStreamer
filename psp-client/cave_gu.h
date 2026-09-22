/* Native cached isosurface geometry; shares the music renderer's GU owner. */
static CaveScene *cave_scene;
static void cave_draw(int width,int height) {
    if(!tunnel_texture_ready) {
        tunnel_texture(tunnel_pixels);tunnel_texture_ready=1;
        sceKernelDcacheWritebackRange(tunnel_pixels,sizeof(tunnel_pixels));
    }
    md_target(MD_TEXTURE_BASE,MD_WIDTH,MD_WIDTH,MD_HEIGHT);
    sceGuDepthBuffer((void *)(uintptr_t)(MD_TEXTURE_BASE+MD_TEXTURE_BYTES),MD_WIDTH);
    sceGuDepthRange(65535,0);sceGuDepthMask(0);sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);
    sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_FOG);sceGuFog(4,14,0xff000000);
    sceGuClearColor(0xff000000);sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexImage(0,TUNNEL_TEXTURE,TUNNEL_TEXTURE,TUNNEL_TEXTURE,tunnel_pixels);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexFlush();
    float x,y;cave_camera(cave_scene->motion.travel,&x,&y);
    ScePspFMatrix4 identity={.x={1,0,0,0},.y={0,1,0,0},.z={0,0,1,0},.w={0,0,0,1}};
    ScePspFMatrix4 view=identity;
    view.w.x=-x;view.w.y=-y;view.w.z=cave_scene->motion.travel;
    ScePspFMatrix4 projection={.x={1.25f*height/width,0,0,0},.y={0,1.25f,0,0},
        .z={0,0,-1.006689f,-1},.w={0,0,-.200669f,0}};
    sceGuSetMatrix(GU_PROJECTION,&projection);sceGuSetMatrix(GU_VIEW,&view);sceGuSetMatrix(GU_MODEL,&identity);
    int first=(int)floorf(cave_scene->motion.travel);
    for(int i=0;i<CAVE_SLICES;i++) {
        CaveSlice *slice=&cave_scene->slices[i];
        if(slice->index<first || !slice->count)continue;
        sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                       slice->count,NULL,slice->vertices);
    }
}
