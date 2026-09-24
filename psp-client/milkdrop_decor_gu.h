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
static int md_shapes(const MdShapeFrame *shapes,float aspect,const MdImage *images,float opacity) {
    int submitted=0;
    for(int slot=0;slot<MD_SHAPES;slot++) for(int instance=0;instance<shapes->count[slot];instance++) {
        MdShape faded=shapes->shapes[slot][instance];
        faded.a*=opacity;faded.a2*=opacity;faded.border_a*=opacity;
        const MdShape *p=&faded;
        if(!p->enabled) continue;
        unsigned int border_color=md_shape_rgba(p->border_r,p->border_g,p->border_b,p->border_a);
        int fill_visible=(md_shape_rgba(p->r,p->g,p->b,p->a)>>24) ||
                         (md_shape_rgba(p->r2,p->g2,p->b2,p->a2)>>24);
        int border_visible=p->border_a>0 && (border_color>>24);
        /* Formula state has already advanced. Zero-alpha layers cannot alter
         * feedback, including additive drawing; keep visible borders alone. */
        if(!fill_visible && !border_visible)continue;
        int sides=md_shape_sides(p->sides);
        if(!sides)continue;
        MdVertex source[MD_SHAPE_SIDES+2];
        int count=md_shape_vertices(source,p,aspect);
        if(!count)continue;
        md_expand(source,count,0);
        int clipped=0;
        float minx=source[0].x,maxx=minx,miny=source[0].y,maxy=miny;
        for(int j=0;j<count;j++) {
            if(source[j].x<minx)minx=source[j].x;
            if(source[j].x>maxx)maxx=source[j].x;
            if(source[j].y<miny)miny=source[j].y;
            if(source[j].y>maxy)maxy=source[j].y;
            if(source[j].x<1 || source[j].x>MD_WIDTH-1 ||
               source[j].y<1 || source[j].y>MD_HEIGHT-1)clipped=1;
        }
        if(maxx<-1 || minx>MD_WIDTH+1 || maxy<-1 || miny>MD_HEIGHT+1)continue;
        /* A clipped triangle can become five triangles; separate original
         * border segments may double border storage. Reserve six ordinary
         * shape slots, keeping the existing fixed GU-list bound. */
        int weight=clipped?6:1;
        if(submitted+weight>MD_SHAPE_BATCH) {
            /* Never reuse vertex/list storage while GE is reading it.
             * A new DIRECT list can restore the SDK's screen framebuffer;
             * explicitly reselect our feedback target before any drawing. */
            sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
            if(sceGuStart(GU_DIRECT,md_list)<0)return 0;
            md_target(md_offset(1-md_front),MD_WIDTH,MD_WIDTH,MD_HEIGHT);
            submitted=0;
        }
        submitted+=weight;
        MdVertex *v=source;
        md_blend(p->additive!=0);
        if(p->textured) {
            sceGuEnable(GU_TEXTURE_2D);
            /* Original fixed-function shapes take alpha from vertex color,
             * not the feedback texture's alpha channel. */
            const MdImage *image=&images[slot];
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
                sceGuTexImage(0,MD_WIDTH,MD_HEIGHT,MD_WIDTH,md_raw_image?md_raw_image:md_texture(md_front));
            }
            sceGuTexFlush();
        } else sceGuDisable(GU_TEXTURE_2D);
        if(clipped) {
            MdVertex *fill=sceGuGetMemory(sides*15*sizeof(*fill));int used=0;
            for(int j=1;j+1<count;j++) {
                MdVertex tri[3]={v[0],v[j],v[j+1]};
                used+=md_clip_triangle(fill+used,tri,MD_WIDTH,MD_HEIGHT);
            }
            if(used)sceGuDrawArray(GU_TRIANGLES,MD_FORMAT,used,NULL,fill);
        } else {
            MdVertex *fill=sceGuGetMemory(count*sizeof(*fill));
            memcpy(fill,v,count*sizeof(*fill));
            sceGuDrawArray(GU_TRIANGLE_FAN,MD_FORMAT,count,NULL,fill);
        }
        if(border_visible) {
            int passes=p->thick_outline!=0?4:1;
            /* Four one-feedback-texel offsets, following MilkDrop 2's
             * fixed-function border. Its positive D3D y is upward; our GU
             * coordinates are downward. Each pass owns immutable vertices:
             * the GE consumes this list asynchronously after submission.
             * One allocation avoids per-pass padding at maximum sides. */
            static const float dx[4]={0,1,1,0},dy[4]={0,0,-1,-1};
            MdVertex *edges=sceGuGetMemory(passes*(clipped?sides*2:count-1)*sizeof(*edges));
            unsigned int color=border_color;
            sceGuDisable(GU_TEXTURE_2D);
            for(int pass=0;pass<passes;pass++) {
                MdVertex *edge=edges+pass*(clipped?sides*2:count-1);
                if(clipped) {
                    int used=0;
                    for(int j=1;j+1<count;j++) {
                        MdVertex line[2]={v[j],v[j+1]};
                        for(int k=0;k<2;k++) {
                            line[k].color=color;line[k].x+=dx[pass];line[k].y+=dy[pass];
                        }
                        used+=md_clip_segment(edge+used,line,MD_WIDTH,MD_HEIGHT);
                    }
                    if(used)sceGuDrawArray(GU_LINES,MD_FORMAT,used,NULL,edge);
                    continue;
                }
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
    MdVertex *batch=sceGuGetMemory(8*sizeof(*batch));
    for(int i=0;i<4;i++) {
        MdVertex *v=batch+2*i;
        v[0]=(MdVertex){0,0,color,rectangles[i][0],rectangles[i][1],0};
        v[1]=(MdVertex){0,0,color,rectangles[i][2],rectangles[i][3],0};
        md_expand(v,2,0);
    }
    sceGuDrawArray(GU_SPRITES,MD_FORMAT,8,NULL,batch);
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
                    v[j].v*=(float)MD_HEIGHT/MD_TEXTURE;
                }
                sceGuDrawArray(GU_TRIANGLES,MD_FORMAT,6,NULL,v);
                continue;
            }
            int strips=(width+31)/32;
            MdVertex *batch=sceGuGetMemory(2*strips*sizeof(*batch));
            for(int x=0;x<width;x+=32) {
                int end=x+32<width?x+32:width;
                MdVertex *v=batch+2*(x/32);
                v[0]=(MdVertex){0,0,tint,(float)(left+x),(float)top,0};
                v[1]=(MdVertex){0,0,tint,(float)(left+end),(float)(top+height),0};
                md_echo_uv((float)x/width,0,zoom,orient,&v[0].u,&v[0].v);
                md_echo_uv((float)end/width,1,zoom,orient,&v[1].u,&v[1].v);
                v[0].u*=2; v[1].u*=2;
                v[0].v*=(float)MD_HEIGHT/MD_TEXTURE; v[1].v*=(float)MD_HEIGHT/MD_TEXTURE;
            }
            if(strips)sceGuDrawArray(GU_SPRITES,MD_FORMAT,2*strips,NULL,batch);
        }
    }
    sceGuDisable(GU_BLEND);
}
static void md_darken_center(float aspect) {
    MdVertex *v=sceGuGetMemory(6*sizeof(*v));
    const float x[]={0,-1,0,1,0,-1},y[]={0,0,-1,0,1,0};
    for(int i=0;i<6;i++) v[i]=(MdVertex){.x=256+x[i]*12.8f*aspect,
        .y=MD_HEIGHT*(.5f+y[i]*.025f),.color=i?0:0x18000000};
    sceGuDisable(GU_TEXTURE_2D); md_blend(0);
    sceGuDrawArray(GU_TRIANGLE_FAN,MD_FORMAT,6,NULL,v); sceGuDisable(GU_BLEND);
}
/* PSP lacks a destination-times-itself blend factor. Copy one 64-wide tile
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
