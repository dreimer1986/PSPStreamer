/* PSP-only fixed-function layers. Included after the GU adapter helpers. */
#ifndef PSPSTREAMER_MILKDROP_DECOR_GU_H
#define PSPSTREAMER_MILKDROP_DECOR_GU_H
/* Filled below the existing helpers; declarations keep adapter ordering clear. */
static void md_darken_center(float aspect);
static void md_image_effects(const float effects[5]);
static void md_blend(int additive) {
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,additive?GU_FIX:GU_ONE_MINUS_SRC_ALPHA,0,additive?0xffffff:0);
}
static int md_shapes(const MdShapeFrame *shapes,float aspect) {
    int submitted=0;
    for(int slot=0;slot<MD_SHAPES;slot++) for(int instance=0;instance<shapes->count[slot];instance++) {
        const MdShape *p=&shapes->shapes[slot][instance];
        if(!p->enabled) continue;
        int sides=md_shape_sides(p->sides);
        if(!sides)continue;
        if(submitted==MD_SHAPE_BATCH) {
            /* Never reuse vertex/list storage while GE is reading it.
             * A new DIRECT list can restore the SDK's screen framebuffer;
             * explicitly reselect our feedback target before any drawing. */
            sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
            if(sceGuStart(GU_DIRECT,md_list)<0)return 0;
            md_target(MD_TEXTURE_BASE+(1-md_front)*MD_TEXTURE_BYTES,MD_WIDTH,MD_WIDTH,MD_HEIGHT);
            submitted=0;
        }
        submitted++;
        MdVertex *v=sceGuGetMemory((sides+2)*sizeof(*v));
        int count=md_shape_vertices(v,p,aspect);
        if(!count) continue;
        md_expand(v,count,0);
        md_blend(p->additive!=0);
        if(p->textured) {
            sceGuEnable(GU_TEXTURE_2D);
            /* Original fixed-function shapes take alpha from vertex color,
             * not the feedback texture's alpha channel. */
            const MdImage *image=&md_images[slot];
            if(image->pixels) {
                for(int j=0;j<count;j++) {
                    v[j].u*= (float)image->width/MD_WIDTH;
                    v[j].v*= (float)image->height/MD_HEIGHT;
                }
                sceGuTexMode(GU_PSM_8888,0,0,1);
                sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
                sceGuTexImage(0,image->width,image->height,image->width,image->pixels);
            } else {
                sceGuTexMode(md_pixel_format,0,0,0);
                sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGB);
                sceGuTexImage(0,MD_WIDTH,MD_HEIGHT,MD_WIDTH,md_texture(md_front));
            }
            sceGuTexFlush();
        } else sceGuDisable(GU_TEXTURE_2D);
        sceGuDrawArray(GU_TRIANGLE_FAN,MD_FORMAT,count,NULL,v);
        unsigned int border_color=md_shape_rgba(p->border_r,p->border_g,p->border_b,p->border_a);
        if(border_color>>24) {
            int passes=p->thick_outline!=0?4:1;
            /* Four one-feedback-texel offsets, following MilkDrop 2's
             * fixed-function border. Its positive D3D y is upward; our GU
             * coordinates are downward. Each pass owns immutable vertices:
             * the GE consumes this list asynchronously after submission.
             * One allocation avoids per-pass padding at maximum sides. */
            static const float dx[4]={0,1,1,0},dy[4]={0,0,-1,-1};
            MdVertex *edges=sceGuGetMemory(passes*(count-1)*sizeof(*edges));
            unsigned int color=border_color;
            sceGuDisable(GU_TEXTURE_2D);
            for(int pass=0;pass<passes;pass++) {
                MdVertex *edge=edges+pass*(count-1);
                for(int j=1;j<count;j++) {
                    edge[j-1]=v[j]; edge[j-1].color=color;
                    edge[j-1].x+=dx[pass]; edge[j-1].y+=dy[pass];
                }
                sceGuDrawArray(GU_LINE_STRIP,MD_FORMAT,count-1,NULL,edge);
            }
        }
    }
    sceGuTexMode(md_pixel_format,0,0,0);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuDisable(GU_TEXTURE_2D); sceGuDisable(GU_BLEND);
    return 1;
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
                       float echo_zoom,float echo_alpha,int orientation,const float shade[4][3]) {
    int layers=echo_alpha>0?2:1,draws=0;
    for(int layer=0;layer<layers;layer++) {
        float mix=layer?echo_alpha:1-echo_alpha;
        float zoom=layer?echo_zoom:1;
        int orient=layer?orientation:0;
        for(int pass=0;pass<(int)(gamma+.999f);pass++) {
            float strength=gamma-pass; if(strength>1) strength=1;
            unsigned int tint=md_rgba(strength*mix,strength*mix,strength*mix,1);
            if(draws++) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,0xffffff,0xffffff); }
            if(shade) {
                /* Same corner order/diagonal as Desktop's two-triangle quad.
                 * Sprites cannot interpolate four independent corner colors.
                 * Apply here once, not in feedback or final screen stretching. */
                static const int corners[6]={0,1,2,1,3,2};
                MdVertex *v=sceGuGetMemory(6*sizeof(*v));
                sceGuShadeModel(GU_SMOOTH);
                for(int j=0;j<6;j++) {
                    int i=corners[j],x=i&1,y=i>>1;
                    v[j]=(MdVertex){.x=(float)(left+x*width),.y=(float)(top+y*height),
                        .color=md_rgba(strength*mix*shade[i][0],strength*mix*shade[i][1],strength*mix*shade[i][2],1)};
                    md_echo_uv((float)x,(float)y,zoom,orient,&v[j].u,&v[j].v);
                    v[j].u*=2;
                }
                sceGuDrawArray(GU_TRIANGLES,MD_FORMAT,6,NULL,v);
                continue;
            }
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
static void md_darken_center(float aspect) {
    MdVertex *v=sceGuGetMemory(6*sizeof(*v));
    const float x[]={0,-1,0,1,0,-1},y[]={0,0,-1,0,1,0};
    for(int i=0;i<6;i++) v[i]=(MdVertex){.x=256+x[i]*12.8f*aspect,.y=128+y[i]*6.4f,.color=i?0:0x18000000};
    sceGuDisable(GU_TEXTURE_2D); md_blend(0);
    sceGuDrawArray(GU_TRIANGLE_FAN,MD_FORMAT,6,NULL,v); sceGuDisable(GU_BLEND);
}
/* PSP lacks a destination-times-itself blend factor. Copy one 64x256 tile
 * of the composed image into spare EDRAM, then use it as the source operand.
 * Never sample the active render target or modify the raw feedback surface. */
static void md_image_effects(const float effects[5]) {
    for(int effect=1;effect<5;effect++) if(effects[effect]) {
        for(int x=0;x<MD_WIDTH;x+=64) {
            sceGuTexSync();
            sceGuCopyImage(md_pixel_format,x,0,64,MD_HEIGHT,MD_WIDTH,md_texture(md_front),
                           0,0,64,md_texture(2));
            sceGuTexSync(); sceGuTexFlush();
            sceGuTexImage(0,64,MD_HEIGHT,64,md_texture(2));
            sceGuTexFilter(GU_NEAREST,GU_NEAREST);
            sceGuEnable(GU_TEXTURE_2D); sceGuEnable(GU_BLEND);
            if(effect==1) sceGuBlendFunc(GU_ADD,GU_ONE_MINUS_OTHER_COLOR,GU_FIX,0,0xffffff);
            if(effect==2) sceGuBlendFunc(GU_ADD,GU_OTHER_COLOR,GU_FIX,0,0);
            if(effect==3) sceGuBlendFunc(GU_ADD,GU_ONE_MINUS_OTHER_COLOR,GU_ONE_MINUS_OTHER_COLOR,0,0);
            if(effect==4) {sceGuDisable(GU_TEXTURE_2D);sceGuBlendFunc(GU_ADD,GU_ONE_MINUS_OTHER_COLOR,GU_FIX,0,0);}
            MdVertex *v=sceGuGetMemory(2*sizeof(*v));
            v[0]=(MdVertex){0,0,0xffffffff,(float)x,0,0};
            v[1]=(MdVertex){64,MD_HEIGHT,0xffffffff,(float)(x+64),MD_HEIGHT,0};
            sceGuDrawArray(GU_SPRITES,MD_FORMAT,2,NULL,v);
        }
    }
    sceGuDisable(GU_BLEND); sceGuTexFilter(GU_LINEAR,GU_LINEAR);
}
#endif
