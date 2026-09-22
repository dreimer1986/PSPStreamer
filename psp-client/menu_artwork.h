/* Optional idle-browser artwork. Uses the existing cancellable HTTP worker:
 * no extra socket/thread, JPEG module, GU state or decoder initialization. */
#define MENU_ART_BYTES (20+320*180*2+80*112*2)
static unsigned char *menu_art_active,*menu_art_pending;
static char menu_art_wanted[ID_SIZE],menu_art_job[ID_SIZE];
static unsigned long long menu_art_since,menu_art_retry_at;
static int menu_art_changed;
static int menu_art_visible,menu_art_pending_reuse;
static char menu_art_tag[65],menu_art_job_tag[65],menu_art_pending_tag[65];
static int menu_art_tag_valid(const unsigned char *p) {
    for(int i=0;i<64;i++)if(!((p[i]>='0' && p[i]<='9') || (p[i]>='a' && p[i]<='f')))return 0;
    return 1;
}
static unsigned int menu_art_u32(const unsigned char *p) {
    return p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);
}
static int menu_art_valid(const unsigned char *p,int size) {
    if(size<20 || memcmp(p,"PSPA\x40\x01\xb4\x00\x50\x00\x70\x00",12))return 0;
    unsigned int bg=menu_art_u32(p+12),cover=menu_art_u32(p+16);
    return (!bg || bg==320*180*2) && (!cover || cover==80*112*2) && size==20+(int)bg+(int)cover;
}
static void menu_art_select(const char *value) {
    if(strncmp(value,"plex.",5) && strncmp(value,"jellyfin.",9) &&
       strncmp(value,":plex:m",7) && strncmp(value,":jellyfin:m",11))value="";
    if(!strcmp(value,menu_art_wanted))return;
    snprintf(menu_art_wanted,sizeof(menu_art_wanted),"%s",value);
    menu_art_retry_at=0;menu_art_since=sceKernelGetSystemTimeWide();
    menu_art_visible=0;menu_art_changed=1;
    /* Retain at most one image while browsing, hidden until the server confirms
     * identity. Playback/non-provider selections still release the allocation. */
    if(!value[0]){free(menu_art_active);menu_art_active=NULL;menu_art_tag[0]=0;}
}
static int menu_art_schedule(void) {
    if(!menu_art_wanted[0] || menu_art_visible || (unsigned long long)sceKernelGetSystemTimeWide()<menu_art_retry_at ||
       sceKernelGetSystemTimeWide()-menu_art_since<500000ULL)return 0;
    snprintf(menu_art_job,sizeof(menu_art_job),"%s",menu_art_wanted);
    memcpy(menu_art_job_tag,menu_art_tag,sizeof(menu_art_job_tag));
    menu_art_retry_at=sceKernelGetSystemTimeWide()+15000000ULL;
    return 1;
}
static void menu_art_download(volatile int *running) {
    /* Identifiers contain only provider prefixes, digits, dots and hex. */
    char path[ID_SIZE+128];
    menu_art_pending_reuse=0;menu_art_pending_tag[0]=0;
    menu_art_pending=malloc(MENU_ART_BYTES+4096);
    if(!menu_art_pending)return;
    snprintf(path,sizeof(path),"/api/psp-artwork?item=%s&v=2&known=%s",menu_art_job,menu_art_job_tag);
    /* Includes TLS plus a 130-KiB packet, not a small control JSON reply.
     * Local actions still cancel; media/stream timeouts are unaffected. */
    int size=remote_http_get_budget(path,(char *)menu_art_pending,MENU_ART_BYTES+4096,running,10000);
    if(*running && size>=68 && menu_art_tag_valid(menu_art_pending+4)) {
        if(size==68 && !memcmp(menu_art_pending,"PSPK",4) && menu_art_job_tag[0] &&
           !memcmp(menu_art_pending+4,menu_art_job_tag,64)) {
            memcpy(menu_art_pending_tag,menu_art_job_tag,65);menu_art_pending_reuse=1;
            free(menu_art_pending);menu_art_pending=NULL;return;
        }
        if(!memcmp(menu_art_pending,"PSPI",4)) {
            memcpy(menu_art_pending_tag,menu_art_pending+4,64);menu_art_pending_tag[64]=0;
            if(!strcmp(menu_art_pending_tag,"0000000000000000000000000000000000000000000000000000000000000000"))
                menu_art_pending_tag[0]=0;
            size-=68;memmove(menu_art_pending,menu_art_pending+68,(size_t)size);
        }
    }
    if(!*running || !menu_art_valid(menu_art_pending,size)) {
        free(menu_art_pending);menu_art_pending=NULL;
    } else {
        unsigned char *small=realloc(menu_art_pending,(size_t)size);
        if(small)menu_art_pending=small;
    }
}
/* Called only after the worker has joined. Never publish a stale selection. */
static void menu_art_complete(int deliver) {
    if(deliver && menu_art_pending_reuse && menu_art_active &&
       !strcmp(menu_art_job,menu_art_wanted) && !strcmp(menu_art_tag,menu_art_pending_tag)) {
        menu_art_visible=1;menu_art_changed=1;
    }
    menu_art_pending_reuse=0;
    /* Do not turn temporary provider/conversion failure into a permanent
     * invisible success. Empty packets retry slowly while selected. */
    if(menu_art_pending && !menu_art_u32(menu_art_pending+12) && !menu_art_u32(menu_art_pending+16)) {
        free(menu_art_pending);menu_art_pending=NULL;
        menu_art_retry_at=sceKernelGetSystemTimeWide()+60000000ULL;
    }
    if(deliver && menu_art_pending && !strcmp(menu_art_job,menu_art_wanted)) {
        free(menu_art_active);menu_art_active=menu_art_pending;menu_art_pending=NULL;
        memcpy(menu_art_tag,menu_art_pending_tag,sizeof(menu_art_tag));
        menu_art_visible=1;menu_art_changed=1;
    }
    free(menu_art_pending);menu_art_pending=NULL;
}
static int menu_art_has_cover(void) {
    return menu_art_visible && menu_art_active && menu_art_u32(menu_art_active+16)!=0;
}
static void menu_art_draw(u32 *pixels,int stride,int x,int y,int width,int height,int cover) {
    if(!menu_art_visible || !menu_art_active || width<=0 || height<=0)return;
    unsigned int bg=menu_art_u32(menu_art_active+12);
    if(cover?!menu_art_has_cover():!bg)return;
    const unsigned char *source=menu_art_active+20+(cover?bg:0);
    int sw=cover?80:320,sh=cover?112:180,sx=0,sy=0;
    if(cover) {
        /* Contain the already letterboxed poster, never distort it. */
        if(width*sh>height*sw){int w=height*sw/sh;x+=(width-w)/2;width=w;}
        else {int h=width*sh/sw;y+=(height-h)/2;height=h;}
    } else {
        /* Center crop rather than stretching between LCD and TV layouts. */
        if(width*sh>height*sw){int h=sw*height/width;sy=(sh-h)/2;sh=h;}
        else {int w=sh*width/height;sx=(sw-w)/2;sw=w;}
    }
    int source_stride=cover?80:320;
    for(int row=0;row<height;row++)for(int col=0;col<width;col++) {
        int at=2*((sy+row*sh/height)*source_stride+sx+col*sw/width);
        unsigned int c=source[at]|((unsigned int)source[at+1]<<8);
        unsigned int r=c>>11,g=(c>>5)&63,b=c&31;
        u32 color=((r<<3)|(r>>2))|(((g<<2)|(g>>4))<<8)|(((b<<3)|(b>>2))<<16);
        u32 *dest=pixels+(y+row)*stride+x+col;
        if(!cover)color=((((color&0xff00ff)+(*dest&0xff00ff)*3)>>2)&0xff00ff)|
                         ((((color&0x00ff00)+(*dest&0x00ff00)*3)>>2)&0x00ff00);
        *dest=color;
    }
}
