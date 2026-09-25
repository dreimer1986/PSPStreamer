/* A transient texture in the effect, never a GUI panel over the scanout. */
static unsigned short *md_title_pixels;
static char md_title_key[400];
static unsigned long long md_title_until;
void md_title(const unsigned char *font,const char *heading,const char *song,
              unsigned long long now,int force) {
    char key[400];snprintf(key,sizeof(key),"%s\n%s",heading,song);
    if(!font || (!force&&!strcmp(key,md_title_key)))return;
    if(!md_title_pixels)md_title_pixels=memalign(64,512*64*2);
    if(!md_title_pixels)return;
    snprintf(md_title_key,sizeof(md_title_key),"%s",key);md_title_until=now+5000000ULL;
    memset(md_title_pixels,0,512*64*2);
    unsigned char glyphs[3][28]={{0}};int length[3]={0},row=0;
    const unsigned char *s=(const unsigned char *)key;
    while(*s && row<3) {
        if(*s=='\n'){s++;row++;continue;}
        if(length[row]==28){row++;if(row==3)break;}
        unsigned int c=*s++;
        if(c>=0xc2 && c<=0xc3 && (*s&0xc0)==0x80)c=((c&31)<<6)|(*s++&63);
        else if(c>=128){while((*s&0xc0)==0x80)s++;c='?';}
        glyphs[row][length[row]++]=(unsigned char)c;
    }
    for(int line=0;line<3;line++)for(int i=0;i<length[line];i++) {
        int c=glyphs[line][i],left=(512-length[line]*16)/2+i*16;
        const unsigned char *glyph=font+(c>>4)*20*256+(c&15)*16;
        for(int y=0;y<20;y++)for(int x=0;x<16;x++) {
            unsigned int alpha=glyph[y*256+x]>>4;
            md_title_pixels[(line*20+y)*512+left+x]=(alpha<<12)|0x0fff;
        }
    }
    sceKernelDcacheWritebackRange(md_title_pixels,512*64*2);
}
static void md_title_draw(unsigned long long now,int cave) {
    if(!md_title_pixels || now>=md_title_until)return;
    unsigned int alpha=(unsigned int)((md_title_until-now)/4000);
    if(alpha>255)alpha=255;
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);
    sceGuTexImage(0,512,64,512,md_title_pixels);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexFlush();
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FOG);sceGuDepthMask(1);
    if(cave && cave_scene) {
        /* Camera-space geometry goes through the tunnel's projection and
         * reference material colour; it is not drawn on the TV/LCD GUI. */
        ScePspFMatrix4 identity={.x={1,0,0,0},.y={0,1,0,0},.z={0,0,1,0},.w={0,0,0,1}};
        sceGuSetMatrix(GU_VIEW,&identity);sceGuSetMatrix(GU_MODEL,&identity);
        unsigned int color=cave_effect_color(cave_scene->paths.seed,cave_scene->motion.travel,0,0,0);
        color=(color&0xffffff)|(alpha<<24);
        MdVertex *v=sceGuGetMemory(4*sizeof(*v));
        float bob=.04f*sinf((float)(now%10000000)/1000000);
        /* GU_TRANSFORM_3D uses normalized UVs, unlike the 2D sprite below. */
        v[0]=(MdVertex){0,0,color,-.7f,.10f+bob,-2};v[1]=(MdVertex){0,1,color,-.7f,-.10f+bob,-2};
        v[2]=(MdVertex){1,0,color,.7f,.10f+bob,-2};v[3]=(MdVertex){1,1,color,.7f,-.10f+bob,-2};
        sceGuDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,4,NULL,v);
    } else {
        /* Inject before echo/gamma and keep it in RAW feedback: subsequent
         * frames warp and decay its trails just like waves and shapes. */
        MdVertex *v=sceGuGetMemory(2*sizeof(*v));
        float h=64.0f*MD_HEIGHT/256;
        v[0]=(MdVertex){0,0,0xffffff|(alpha<<24),0,(MD_HEIGHT-h)/2,0};
        v[1]=(MdVertex){512,64,0xffffff|(alpha<<24),512,(MD_HEIGHT+h)/2,0};
        sceGuDrawArray(GU_SPRITES,MD_FORMAT,2,NULL,v);
    }
    sceGuDisable(GU_BLEND);
    sceGuTexMode(md_pixel_format,0,0,0);
}
