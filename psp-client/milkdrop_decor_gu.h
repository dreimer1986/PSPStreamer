/* PSP-only fixed-function layers. Included after the GU adapter helpers. */
#ifndef PSPSTREAMER_MILKDROP_DECOR_GU_H
#define PSPSTREAMER_MILKDROP_DECOR_GU_H
static void md_blend(int additive) {
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,additive?GU_FIX:GU_ONE_MINUS_SRC_ALPHA,0,additive?0xffffff:0);
}
static void md_shapes(const MdDecor *decor,float aspect) {
    for(int i=0;i<MD_SHAPES;i++) {
        const MdShape *p=&decor->shapes[i];
        if(!p->enabled) continue;
        MdVertex *v=sceGuGetMemory(((int)p->sides+2)*sizeof(*v));
        int count=md_shape_vertices(v,p,aspect);
        if(!count) continue;
        md_expand(v,count,0);
        md_blend(p->additive!=0);
        if(p->textured) {
            sceGuEnable(GU_TEXTURE_2D);
            /* Original fixed-function shapes take alpha from vertex color,
             * not the feedback texture's alpha channel. */
            sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGB);
            sceGuTexImage(0,MD_WIDTH,MD_HEIGHT,MD_WIDTH,md_texture(md_front));
            sceGuTexFlush();
        } else sceGuDisable(GU_TEXTURE_2D);
        sceGuDrawArray(GU_TRIANGLE_FAN,MD_FORMAT,count,NULL,v);
        if(p->border_a>0) {
            MdVertex *edge=sceGuGetMemory((count-1)*sizeof(*edge));
            unsigned int color=md_rgba(p->border_r,p->border_g,p->border_b,p->border_a);
            for(int j=1;j<count;j++) { edge[j-1]=v[j]; edge[j-1].color=color; }
            sceGuDisable(GU_TEXTURE_2D);
            sceGuDrawArray(GU_LINE_STRIP,MD_FORMAT,count-1,NULL,edge);
        }
    }
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuDisable(GU_TEXTURE_2D); sceGuDisable(GU_BLEND);
}
static void md_border(const MdBorder *p,float inset) {
    if(p->a<=0 || p->size<=0) return;
    float a=inset*128,b=a+p->size*128;
    float rectangles[4][4]={{a,a,256-a,b},{a,256-b,256-a,256-a},
                           {a,b,b,256-b},{256-b,b,256-a,256-b}};
    md_blend(0);
    unsigned int color=md_rgba(p->r,p->g,p->b,p->a);
    for(int i=0;i<4;i++) {
        MdVertex *v=sceGuGetMemory(2*sizeof(*v));
        v[0]=(MdVertex){0,0,color,rectangles[i][0],rectangles[i][1],0};
        v[1]=(MdVertex){0,0,color,rectangles[i][2],rectangles[i][3],0};
        md_expand(v,2,0);
        sceGuDrawArray(GU_SPRITES,MD_FORMAT,2,NULL,v);
    }
    sceGuDisable(GU_BLEND);
}
static void md_present(int left,int top,int width,int height,float gamma,
                       float echo_zoom,float echo_alpha,int orientation) {
    int layers=echo_alpha>0?2:1,draws=0;
    for(int layer=0;layer<layers;layer++) {
        float mix=layer?echo_alpha:1-echo_alpha;
        float zoom=layer?echo_zoom:1;
        int orient=layer?orientation:0;
        for(int pass=0;pass<(int)(gamma+.999f);pass++) {
            float strength=gamma-pass; if(strength>1) strength=1;
            unsigned int tint=md_rgba(strength*mix,strength*mix,strength*mix,1);
            if(draws++) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,0xffffff,0xffffff); }
            for(int x=0;x<width;x+=32) {
                int end=x+32<width?x+32:width;
                MdVertex *v=sceGuGetMemory(2*sizeof(*v));
                v[0]=(MdVertex){0,0,tint,(float)(left+x),(float)top,0};
                v[1]=(MdVertex){0,0,tint,(float)(left+end),(float)(top+height),0};
                md_echo_uv((float)x/width,0,zoom,orient,&v[0].u,&v[0].v);
                md_echo_uv((float)end/width,1,zoom,orient,&v[1].u,&v[1].v);
                v[0].u*=2; v[1].u*=2;
                sceGuDrawArray(GU_SPRITES,MD_FORMAT,2,NULL,v);
            }
        }
    }
    sceGuDisable(GU_BLEND);
}
#endif
