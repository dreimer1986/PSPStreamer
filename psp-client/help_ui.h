/* Lightweight diagram drawn directly at the output resolution. No external
 * image, network request, decoder, or playback worker is needed by help. */
static int help_px(int value) {return tv_ui_active?value*3/2:value;}
static void help_rect(int x,int y,int w,int h,u32 color) {
    if(tv_ui_active)tv_rect(&tv_canvas,help_px(x),help_px(y),help_px(w),help_px(h),color);
    else gui_rect((u32 *)0x44000000,x,y,w,h,color);
}
static void help_line(int x,int y,int ex,int ey,u32 color) {
    if(tv_ui_active) {tv_line(&tv_canvas,help_px(x),help_px(y),help_px(ex),help_px(ey),color);return;}
    int dx=abs(ex-x),sx=x<ex?1:-1,dy=-abs(ey-y),sy=y<ey?1:-1,error=dx+dy;
    for(;;) {
        help_rect(x,y,1,1,color);if(x==ex && y==ey)break;
        int twice=error*2;
        if(twice>=dy){error+=dy;x+=sx;}
        if(twice<=dx){error+=dx;y+=sy;}
    }
}
static void help_disc(int cx,int cy,int radius,u32 color) {
    /* Rasterize circles natively, including native TV-size button outlines. */
    int x=help_px(cx),y=help_px(cy),r=help_px(radius);
    for(int yy=-r;yy<=r;yy++)for(int xx=-r;xx<=r;xx++)if(xx*xx+yy*yy<=r*r) {
        if(tv_ui_active)tv_pixel(&tv_canvas,x+xx,y+yy,color);
        else gui_rect((u32 *)0x44000000,x+xx,y+yy,1,1,color);
    }
}
static void help_text(int x,int y,u32 color,const char *text) {
    if(tv_ui_active)tv_text(help_px(x),help_px(y),54,1,color,"%s",text);
    else gui_text(x,y,color,"%s",text);
}
static u32 help_key_color(unsigned int mask,unsigned int active) {
    return (active&mask)?0x0000D8FF:0x00708088;
}
static void help_psp(unsigned int active) {
    const u32 body=0x002E343C,edge=0x00606E78,ink=0x00101820;
    help_disc(37,126,29,edge);help_disc(143,126,29,edge);
    help_rect(37,97,106,59,edge);
    help_disc(37,126,27,body);help_disc(143,126,27,body);
    help_rect(37,99,106,55,body);
    help_rect(49,104,83,43,0x00070C10);
    help_rect(53,108,75,35,0x0025333A);
    help_text(79,123,0x00C0CCD4,"PSP");
    help_rect(18,92,32,5,help_key_color(PSP_CTRL_LTRIGGER,active));
    help_rect(131,92,32,5,help_key_color(PSP_CTRL_RTRIGGER,active));
    help_text(27,80,help_key_color(PSP_CTRL_LTRIGGER,active),"L");
    help_text(143,80,help_key_color(PSP_CTRL_RTRIGGER,active),"R");
    help_rect(28,110,7,8,help_key_color(PSP_CTRL_UP,active));
    help_rect(28,127,7,8,help_key_color(PSP_CTRL_DOWN,active));
    help_rect(19,119,8,7,help_key_color(PSP_CTRL_LEFT,active));
    help_rect(36,119,8,7,help_key_color(PSP_CTRL_RIGHT,active));
    help_disc(32,144,6,edge);help_disc(32,144,4,ink);
    help_disc(148,111,7,help_key_color(PSP_CTRL_TRIANGLE,active));
    help_line(148,107,144,114,ink);help_line(144,114,152,114,ink);help_line(152,114,148,107,ink);
    help_disc(160,123,7,help_key_color(PSP_CTRL_CIRCLE,active));
    help_disc(160,123,4,ink);help_disc(160,123,2,help_key_color(PSP_CTRL_CIRCLE,active));
    help_disc(136,123,7,help_key_color(PSP_CTRL_SQUARE,active));
    help_rect(133,120,7,7,ink);help_rect(134,121,5,5,help_key_color(PSP_CTRL_SQUARE,active));
    help_disc(148,136,7,help_key_color(PSP_CTRL_CROSS,active));
    help_line(145,133,151,139,ink);help_line(145,139,151,133,ink);
    help_rect(91,151,11,4,help_key_color(PSP_CTRL_SELECT,active));
    help_rect(116,151,11,4,help_key_color(PSP_CTRL_START,active));
    help_line(95,157,81,169,edge);help_line(122,157,131,169,edge);
    help_text(52,173,help_key_color(PSP_CTRL_SELECT,active),"SELECT");
    help_text(118,173,help_key_color(PSP_CTRL_START,active),"START");
}
static void help_draw(int page) {
    const HelpText *text=help_translation()+page;
    char position[64];
    int topic=help_pages[page].topic,first=help_first_page(topic),end=first;
    while(end<HELP_PAGE_COUNT && help_pages[end].topic==topic)end++;
    /* Deliberately opaque: no receiver controls behind instructional text. */
    if(tv_ui_active)tv_rect(&tv_canvas,0,0,720,480,0x00101820);
    else gui_rect((u32 *)0x44000000,0,0,480,272,0x00101820);
    help_rect(0,0,480,31,0x0025313D);
    help_text(14,12,0x0000D8FF,tr(TXT_HELP));
    snprintf(position,sizeof(position),tr(TXT_HELP_POSITION),topic+1,HELP_TOPICS,page-first+1,end-first);
    help_text(178,12,0x00FFFFFF,position);
    help_text(14,45,0x00FFFFFF,text->title);
    help_psp(help_pages[page].buttons);
    help_text(14,200,0x0000D8FF,tr(TXT_HELP_KEYS));
    const char *titles[]={text->step1_title,text->step2_title,text->step3_title};
    const char *lines[]={text->step1_text,text->step2_text,text->step3_text};
    for(int i=0;i<3;i++) {
        help_text(186,80+i*42,0x0000D8FF,titles[i]);
        help_text(186,94+i*42,0x00FFFFFF,lines[i]);
    }
    help_rect(12,222,456,1,0x00445464);
    help_text(14,234,0x00FFFFFF,tr(TXT_HELP_NAV));
    help_text(14,252,0x00B0BDC8,tr(TXT_HELP_BACK));
    if(tv_ui_active)tv_present();
}
static void help_open(int topic) {
    int page=help_first_page(topic),dirty=1;
    SceCtrlData pad;sceCtrlReadBufferPositive(&pad,1);
    unsigned int old=pad.Buttons;
    unsigned long long repeat=0;
    while(1) {
        keep_awake();
        if(dirty){help_draw(page);dirty=0;}
        sceCtrlReadBufferPositive(&pad,1);
        unsigned int pressed=pad.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)return;
        unsigned int move=pad.Buttons&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_UP|PSP_CTRL_DOWN);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(move && ((pressed&move)||now>=repeat)) {
            int topics=(move&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER))!=0;
            int direction=(move&(PSP_CTRL_LTRIGGER|PSP_CTRL_UP))?-1:1;
            page=help_move(page,topics,direction);dirty=1;
            repeat=now+((pressed&move)?400000ULL:200000ULL);
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
