/* Main-menu only: no media/remote workers run while settings are edited. */
enum {SET_HOST,SET_PORT,SET_PASSWORD,SET_HTTPS,SET_LANGUAGE,SET_TV,SET_AUDIO,
      SET_SUBTITLE,SET_QUALITY,SET_FPS,SET_VOLUME,SET_SHUFFLE,SET_PRESET,
      SET_AUTO,SET_SECONDS,SET_FADE,SET_DEBUG,SET_COUNT};
typedef struct {int value[SET_COUNT];char host[64],password[129],preset[256];} AppSettings;
static void settings_capture(AppSettings *s) {
    memset(s,0,sizeof(*s));
    strcpy(s->host,server_host);strcpy(s->password,server_password);strcpy(s->preset,music_preset_file);
    int values[SET_COUNT]={0,server_port,0,server_https,!strcmp(language_code(),"de"),tv_ui_auto,
        selected_audio_track,selected_subtitle_track,selected_audio_quality,selected_video_fps,
        playback_volume,audio_shuffle,0,music_preset_auto,music_preset_seconds,music_preset_fade_ms,debug_enabled};
    memcpy(s->value,values,sizeof(values));
}
static void settings_apply(const AppSettings *s) {
    strcpy(server_host,s->host);strcpy(server_password,s->password);strcpy(music_preset_file,s->preset);
    server_port=s->value[SET_PORT];server_https=s->value[SET_HTTPS];
    language_set_code(s->value[SET_LANGUAGE]?"de":"en");tv_ui_auto=s->value[SET_TV];
    selected_audio_track=s->value[SET_AUDIO];selected_subtitle_track=s->value[SET_SUBTITLE];
    selected_audio_quality=s->value[SET_QUALITY];selected_video_fps=s->value[SET_FPS];
    playback_volume=s->value[SET_VOLUME];audio_shuffle=s->value[SET_SHUFFLE];
    music_preset_auto=s->value[SET_AUTO];music_preset_seconds=s->value[SET_SECONDS];music_preset_fade_ms=s->value[SET_FADE];
    debug_enabled=s->value[SET_DEBUG];
    server_auth_update();
}
static void settings_shell(const char *title) {
    if(tv_ui_active)tv_shell(title);else gui_library_shell(title);
}
static void settings_line(int row,int selected,const char *text) {
    if(tv_ui_active)tv_text(34,72+row*22,48,1,selected?TV_AMBER:TV_WHITE,"%s",text);
    else gui_text(38,48+row*12,selected?0x0000D8FF:0x00FFFFFF,"%.49s",text);
}
static void settings_help(const char *text) {
    if(tv_ui_active) {tv_help(text);tv_present();}
    else gui_text(38,177,0x00FFFFFF,"%.54s",text);
}
static int settings_text(char *text,int capacity,int secret,const char *title) {
    char draft[256];unsigned int old=PSP_CTRL_CROSS;
    int key=0,dirty=1;
    unsigned long long repeat=0;
    snprintf(draft,sizeof(draft),"%s",text);
    /* Printable ASCII plus German letters, encoded as UTF-8 on insertion. */
    static const int extra[]={0xE4,0xF6,0xFC,0xC4,0xD6,0xDC,0xDF};
    while(1) {
        SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);
        unsigned int pressed=pad.Buttons&~old;
        if(dirty) {
            char display[64];
            settings_shell(title);
            if(secret) {int n=strlen(draft);if(n>40)n=40;memset(display,'*',n);display[n]=0;}
            else snprintf(display,sizeof(display),"%.48s",draft);
            settings_line(0,0,display);
            for(int i=0;i<102;i++) {
                int cp=i<95?i+32:extra[i-95]; char glyph[3]={0};
                if(cp<128)glyph[0]=cp==32?'_':cp;
                else {glyph[0]=0xC0|(cp>>6);glyph[1]=0x80|(cp&63);}
                if(tv_ui_active)tv_text(34+i%12*40,106+i/12*20,2,1,i==key?TV_AMBER:TV_WHITE,"%s",glyph);
                else gui_text(40+i%12*25,70+i/12*10,i==key?0x0000D8FF:0x00FFFFFF,"%s",glyph);
            }
            settings_help(tr(TXT_KEYBOARD_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE) {memset(draft,0,sizeof(draft));return 0;}
        if(pressed&PSP_CTRL_START) {strcpy(text,draft);memset(draft,0,sizeof(draft));return 1;}
        if(pressed&PSP_CTRL_LTRIGGER) {int n=strlen(draft);if(n){do{n--;}while(n>0 && (draft[n]&0xC0)==0x80);draft[n]=0;}dirty=1;}
        if(pressed&PSP_CTRL_RTRIGGER) {draft[0]=0;dirty=1;}
        if(pressed&PSP_CTRL_CROSS) {
            int cp=key<95?key+32:extra[key-95],n=strlen(draft),bytes=cp<128?1:2;
            if(n+bytes<capacity) {if(bytes==1)draft[n++]=cp;else{draft[n++]=0xC0|(cp>>6);draft[n++]=0x80|(cp&63);}draft[n]=0;}dirty=1;
        }
        unsigned int movement=pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(movement && ((pressed&movement)||now>=repeat)) {
            int delta=movement&PSP_CTRL_UP?-12:movement&PSP_CTRL_DOWN?12:movement&PSP_CTRL_LEFT?-1:1;
            key=(key+delta+102)%102;repeat=now+150000;dirty=1;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
static int app_settings(void) {
    AppSettings original,draft;settings_capture(&original);draft=original;
    int selected=0,dirty=1,result=0;
    unsigned int old=PSP_CTRL_SELECT;unsigned long long repeat=0;
    static const int minimum[SET_COUNT]={0,1,0,0,0,0,0,-1,0,0,0,0,0,0,30,0,0};
    static const int maximum[SET_COUNT]={0,65535,0,1,1,1,7,31,6,1,30,1,0,3,600,5000,1};
    while(1) {
        keep_awake();SceCtrlData pad;sceCtrlReadBufferPositive(&pad,1);
        unsigned int pressed=pad.Buttons&~old;
        if(dirty) {
            settings_shell(tr(TXT_SETTINGS));
            for(int i=selected/8*8;i<SET_COUNT && i<selected/8*8+8;i++) {
                char value[64],line[128];
                if(i==SET_HOST)snprintf(value,sizeof(value),"%s",draft.host);
                else if(i==SET_PASSWORD)strcpy(value,draft.password[0]?"********":"-");
                else if(i==SET_PRESET)snprintf(value,sizeof(value),"%.48s",draft.preset);
                else if(i==SET_LANGUAGE)strcpy(value,draft.value[i]?"Deutsch":"English");
                else if(i==SET_HTTPS||i==SET_TV||i==SET_SHUFFLE||i==SET_DEBUG)snprintf(value,sizeof(value),"%s",tr(draft.value[i]?TXT_SETTINGS_ON:TXT_OFF));
                else if(i==SET_AUTO)snprintf(value,sizeof(value),"%s",tr((TextId)(TXT_PRESET_AUTO_OFF+draft.value[i])));
                else if(i==SET_FPS)strcpy(value,draft.value[i]?"23.976":"20");
                else if(i==SET_QUALITY) {const char *q[]={"96k","128k","160k","V6","V5","V4","V3"};strcpy(value,q[draft.value[i]]);}
                else snprintf(value,sizeof(value),"%d",draft.value[i]);
                snprintf(line,sizeof(line),"%c %s: %s",i==selected?'>':' ',tr((TextId)(TXT_SETTINGS_HOST+i)),value);
                settings_line(i%8,i==selected,line);
            }
            settings_line(8,0,tr(TXT_SETTINGS_HELP));
            settings_help(tr(TXT_SETTINGS_SAVE_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE)break;
        if(pressed&PSP_CTRL_START) {
            settings_apply(&draft);
            result=save_playback_settings();
            if(result<0)settings_apply(&original);
            else if(strcmp(original.host,draft.host)||strcmp(original.password,draft.password)||
                    original.value[SET_PORT]!=draft.value[SET_PORT]||original.value[SET_HTTPS]!=draft.value[SET_HTTPS]) {
                have_cached_server_address=0;resume_pending=0;remote_control_sequence=0;
                remote_session[0]=0;current_path[0]=0;item_count=0;
            }
            snprintf(status,sizeof(status),"%s",tr(result<0?TXT_SETTINGS_FAILED:TXT_SETTINGS_SAVED));
            result=result<0?-1:1;break;
        }
        if(pressed&PSP_CTRL_CROSS) {
            char input[256];int valid=1;
            const char *initial=selected==SET_HOST?draft.host:selected==SET_PASSWORD?draft.password:selected==SET_PRESET?draft.preset:NULL;
            if(initial)snprintf(input,sizeof(input),"%s",initial);else snprintf(input,sizeof(input),"%d",draft.value[selected]);
            if(settings_text(input,selected==SET_HOST?64:selected==SET_PRESET?256:129,selected==SET_PASSWORD,tr((TextId)(TXT_SETTINGS_HOST+selected)))) {
                if(selected==SET_HOST) {
                    valid=input[0]!=0;
                    for(const char *p=input;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'||*p=='_'))valid=0;
                    if(valid)strcpy(draft.host,input);
                } else if(selected==SET_PASSWORD)strcpy(draft.password,input);
                else if(selected==SET_PRESET) {valid=preset_name_valid(input);if(valid)strcpy(draft.preset,input);}
                else {char *end;long value=strtol(input,&end,10);valid=input[0]&&!*end&&value>=minimum[selected]&&value<=maximum[selected];if(valid)draft.value[selected]=value;}
                if(!valid) {settings_shell(tr(TXT_INVALID_SETTING));settings_help(tr(TXT_INVALID_SETTING));sceKernelDelayThread(700000);}
            }
            memset(input,0,sizeof(input));dirty=1;old=PSP_CTRL_START|PSP_CTRL_CIRCLE|PSP_CTRL_CROSS;continue;
        }
        unsigned int movement=pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(movement && ((pressed&movement)||now>=repeat)) {
            if(movement&PSP_CTRL_UP)selected=(selected+SET_COUNT-1)%SET_COUNT;
            else if(movement&PSP_CTRL_DOWN)selected=(selected+1)%SET_COUNT;
            else if(maximum[selected]) {
                int step=selected==SET_FADE?100:1;
                int value=draft.value[selected]+((movement&PSP_CTRL_LEFT)?-step:step);
                if(value<minimum[selected])value=maximum[selected];
                if(value>maximum[selected])value=minimum[selected];
                draft.value[selected]=value;
            }
            repeat=now+150000;dirty=1;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
    memset(&original,0,sizeof(original));memset(&draft,0,sizeof(draft));return result;
}
