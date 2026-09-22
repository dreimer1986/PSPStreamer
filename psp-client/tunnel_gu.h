/* Original tunnel uses the existing music-only GU owner and offscreen target.
 * No display-mode changes, audio calls, feedback or shaders. */
static TunnelState tunnel_state;
static uint32_t tunnel_pixels[TUNNEL_TEXTURE*TUNNEL_TEXTURE] __attribute__((aligned(64)));
static int tunnel_texture_ready;
static void tunnel_draw(const unsigned char bands[12],int level,unsigned long long now,int width,int height) {
    if(!tunnel_texture_ready) {
        tunnel_texture(tunnel_pixels);tunnel_texture_ready=1;
        sceKernelDcacheWritebackRange(tunnel_pixels,sizeof(tunnel_pixels));
    }
    md_target(md_offset(md_front),MD_WIDTH,MD_WIDTH,MD_HEIGHT);
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_LIGHTING);
    sceGuDepthRange(65535,0);sceGuDepthMask(1); /* No depth buffer is allocated. */
    sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);
    sceGuDisable(GU_FOG);sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_CLIP_PLANES);
    sceGuClearColor(0xff000000);sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexImage(0,TUNNEL_TEXTURE,TUNNEL_TEXTURE,TUNNEL_TEXTURE,tunnel_pixels);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexFlush();
    /* Column-major perspective; GU clips triangles crossing the near plane.
     * Aspect uses the final display rectangle, not the intermediate texture. */
    ScePspFMatrix4 identity={.x={1,0,0,0},.y={0,1,0,0},.z={0,0,1,0},.w={0,0,0,1}};
    ScePspFMatrix4 projection={.x={1.25f*height/width,0,0,0},.y={0,1.25f,0,0},
        .z={0,0,-1.006689f,-1},.w={0,0,-.200669f,0}};
    sceGuSetMatrix(GU_PROJECTION,&projection);sceGuSetMatrix(GU_VIEW,&identity);sceGuSetMatrix(GU_MODEL,&identity);
    MdVertex *mesh=sceGuGetMemory(TUNNEL_VERTICES*sizeof(*mesh));
    int count=tunnel_mesh(&tunnel_state,mesh,TUNNEL_VERTICES,bands,level,now);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,NULL,mesh);
    sceGuTexSync();
}
