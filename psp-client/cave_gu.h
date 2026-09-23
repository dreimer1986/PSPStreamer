/* Native cached isosurface geometry; shares the music renderer's GU owner. */
static CaveScene *cave_scene;
static uint32_t cave_pixels[CAVE_TEXTURE*CAVE_TEXTURE] __attribute__((aligned(64)));
static int cave_texture_ready;
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
    for(int i=0;i<CAVE_SLICES;i++) {
        CaveSlice *slice=&cave_scene->slices[i];
        if(slice->index<first || !slice->count)continue;
        sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,
                       slice->count,NULL,slice->vertices);
    }
}
