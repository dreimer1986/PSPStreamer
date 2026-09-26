/* Two inset upper rear exhaust faces of the supplied ship, in model space.
 * Recolour existing triangles: no extra geometry, textures or overdraw, and
 * ordinary depth/near-plane clipping still hides engines behind tunnel walls. */
static unsigned int cave_engine_color(float x,float y,float z,unsigned int color,float glow) {
    if(z<.300f || z>.306f || y<.013f || y>.074f ||
       (x>-.023f && x<.023f) || x<-.111f || x>.111f ||
       (color&255)<180 || ((color>>16)&255)>20)return color;
    if(!(glow>=0))glow=0;
    if(glow>1)glow=1;
    /* Warm amber rim and brighter centre, always lit even during silence. */
    float dx=(x<0?x+.0666f:x-.0668f)/.044f,dy=(y-.037f)/.037f;
    float core=1.f-dx*dx-dy*dy;if(core<0)core=0;
    unsigned int red=(unsigned int)(155+100*glow);
    unsigned int green=(unsigned int)(48+150*glow+35*core);
    unsigned int blue=(unsigned int)(8+130*glow+70*core);
    return 0xff000000U|red|(green<<8)|(blue<<16);
}
