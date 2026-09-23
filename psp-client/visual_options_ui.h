/* Music workers keep running; caller has released the GU. */
static void music_visual_options(int cave) {
    int first=cave?0:VISUAL_CAVE_OPTIONS,count=cave?VISUAL_CAVE_OPTIONS:VISUAL_OPTION_COUNT-VISUAL_CAVE_OPTIONS,row=0,dirty=1;
    unsigned old=PSP_CTRL_CIRCLE|PSP_CTRL_SELECT;unsigned long long repeat=0;
    while(audio_running && music_remote_action<MUSIC_REMOTE_STOP) {
        keep_awake();video_watch_ping("visual options");
        if(music_remote_action==MUSIC_REMOTE_PAUSE || music_remote_action==MUSIC_REMOTE_RESUME) {
            audio_start=music_remote_action==MUSIC_REMOTE_RESUME;music_remote_action=MUSIC_REMOTE_NONE;
        }
        if(dirty) {
            int top=row/8*8;
            if(tv_ui_active)tv_shell(tr(TXT_VISUAL_OPTIONS));else gui_library_shell(tr(TXT_VISUAL_OPTIONS));
            for(int i=top;i<count && i<top+8;i++) {
                VisualOption *o=&visual_options[first+i];char line[96],value[20];
                if(o->maximum==1)snprintf(value,sizeof(value),"%s",tr(*o->value?TXT_SETTINGS_ON:TXT_OFF));
                else snprintf(value,sizeof(value),"%d",*o->value);
                snprintf(line,sizeof(line),"%s: %s",tr(o->label),value);
                if(tv_ui_active)tv_text(34,72+(i-top)*22,48,1,i==row?TV_AMBER:TV_WHITE,"%s",line);
                else gui_text(38,48+(i-top)*14,i==row?0x0000D8FF:0x00FFFFFF,"%.45s",line);
            }
            if(count>8) {
                if(tv_ui_active)tv_text(560,80,10,1,TV_WHITE,"%d/%d",row+1,count);
                else gui_text(380,70,0x00FFFFFF,"%d/%d",row+1,count);
            }
            if(tv_ui_active){tv_help(tr(TXT_VISUAL_OPTIONS_HELP));tv_present();}
            else gui_text(38,177,0x00FFFFFF,"%s",tr(TXT_VISUAL_OPTIONS_HELP));
            dirty=0;
        }
        SceCtrlData pad;sceCtrlPeekBufferPositive(&pad,1);unsigned pressed=pad.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)break;
        if(pressed&PSP_CTRL_START){music_remote_action=MUSIC_REMOTE_STOP;break;}
        unsigned move=pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(move && ((pressed&move)||now>=repeat)) {
            VisualOption *o=&visual_options[first+row];
            if(move&PSP_CTRL_UP)row=(row+count-1)%count;
            else if(move&PSP_CTRL_DOWN)row=(row+1)%count;
            else {
                *o->value+=(move&PSP_CTRL_LEFT)?-o->step:o->step;
                if(*o->value<o->minimum)*o->value=o->minimum;
                if(*o->value>o->maximum)*o->value=o->maximum;
            }
            dirty=1;repeat=now+((pressed&move)?400000:150000);
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
    if(save_playback_settings()<0)snprintf(status,sizeof(status),"%s",tr(TXT_SETTINGS_FAILED));
}
