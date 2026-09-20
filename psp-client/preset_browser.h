/* Music remains serviced by its existing workers. Called with GU stopped. */
#ifndef PSP_STREAMER_PRESET_BROWSER_H
#define PSP_STREAMER_PRESET_BROWSER_H
static int preset_choose(char selection[256],int music) {
    PresetCatalog *catalog=malloc(sizeof(*catalog));
    int selected=0,dirty=1,changed=0;
    unsigned int old=PSP_CTRL_CIRCLE|PSP_CTRL_CROSS;
    unsigned long long repeat=0;
    if(!catalog) return 0;
    char folder[256]="",directory[272];
    const char *base=selection,*slash=strrchr(selection,'/');
    if(slash && preset_path_valid(selection)){size_t n=slash-selection+1;memcpy(folder,selection,n);folder[n]=0;base=slash+1;}
    snprintf(directory,sizeof(directory),"presets/%s",folder);
    preset_catalog_browse(catalog,directory,*folder!=0);
    for(int i=0;i<catalog->count;i++) if(!strcmp(catalog->names[i],base)) selected=i;
    while(!music || (audio_running && music_remote_action<MUSIC_REMOTE_STOP)) {
        if(music && (music_remote_action==MUSIC_REMOTE_PAUSE || music_remote_action==MUSIC_REMOTE_RESUME)) {
            audio_start=music_remote_action==MUSIC_REMOTE_RESUME;
            music_remote_action=MUSIC_REMOTE_NONE;
        }
        SceCtrlData pad; keep_awake(); video_watch_ping("preset browser");
        if(dirty) {
            int first=selected/8*8;
            if(tv_ui_active) {
                tv_shell(tr(TXT_PRESETS));
                for(int i=first;i<catalog->count && i<first+8;i++)
                    tv_text(34,72+(i-first)*22,38,1,i==selected?TV_AMBER:TV_WHITE,"%c %s",i==selected?'>':' ',catalog->names[i]);
                if(!catalog->count) tv_text(34,72,38,1,TV_WHITE,"%s",tr(TXT_PRESET_MISSING));
                if(catalog->truncated) tv_text(562,80,10,3,TV_AMBER,"%s",tr(TXT_PRESET_LIMIT));
                if(music) {
                    tv_text(34,240,48,1,TV_WHITE,tr(TXT_PRESET_AUTO_STATUS),tr((TextId)(TXT_PRESET_AUTO_OFF+music_preset_auto)),music_preset_seconds);
                    tv_text(34,262,48,1,TV_WHITE,"%s",tr(TXT_PRESET_AUTO_HELP));
                }
                tv_help(tr(TXT_PRESET_CONTROLS)); tv_present();
            } else {
                gui_library_shell(tr(TXT_PRESETS));
                for(int i=first;i<catalog->count && i<first+8;i++)
                    gui_text(38,48+(i-first)*14,i==selected?0x0000D8FF:0x00FFFFFF,"%c %.37s",i==selected?'>':' ',catalog->names[i]);
                if(!catalog->count) gui_text(38,48,0x00FFFFFF,"%s",tr(TXT_PRESET_MISSING));
                if(catalog->truncated) gui_text(376,70,0x0000D8FF,"%s",tr(TXT_PRESET_LIMIT));
                gui_text(38,177,0x00FFFFFF,"%s",tr(TXT_PRESET_CONTROLS));
                if(music) {
                    gui_text(38,156,0x00FFFFFF,tr(TXT_PRESET_AUTO_STATUS),tr((TextId)(TXT_PRESET_AUTO_OFF+music_preset_auto)),music_preset_seconds);
                    gui_text(38,166,0x00FFFFFF,"%s",tr(TXT_PRESET_AUTO_HELP));
                }
            }
            dirty=0;
        }
        sceCtrlPeekBufferPositive(&pad,1);
        unsigned int buttons=pad.Buttons;
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(music && (buttons&PSP_CTRL_SQUARE) && !(old&PSP_CTRL_SQUARE)) {music_preset_auto=(music_preset_auto+1)%4;dirty=1;}
        if(music && (buttons&PSP_CTRL_TRIANGLE) && !(old&PSP_CTRL_TRIANGLE)) {music_preset_seconds=music_preset_seconds==30?60:music_preset_seconds==60?120:30;dirty=1;}
        if((buttons&PSP_CTRL_CIRCLE) && !(old&PSP_CTRL_CIRCLE)) break;
        if((buttons&PSP_CTRL_START) && !(old&PSP_CTRL_START)) { if(music)music_remote_action=MUSIC_REMOTE_STOP; break; }
        if((buttons&PSP_CTRL_CROSS) && !(old&PSP_CTRL_CROSS) && catalog->count) {
            const char *name=catalog->names[selected];size_t n=strlen(name);
            if(!strcmp(name,"..")) {
                size_t length=strlen(folder);if(length)folder[length-1]=0;
                char *parent=strrchr(folder,'/');if(parent)parent[1]=0;else folder[0]=0;
            } else if(n && name[n-1]=='/') {
                if(strlen(folder)+n>=sizeof(folder)){old=buttons;continue;}
                strcat(folder,name);
            } else {
                char candidate[512];snprintf(candidate,sizeof(candidate),"%s%.255s",folder,name);
                if(preset_path_valid(candidate)){strcpy(selection,candidate);changed=1;break;}
                old=buttons;continue;
            }
            snprintf(directory,sizeof(directory),"presets/%s",folder);
            preset_catalog_browse(catalog,directory,*folder!=0);selected=0;dirty=1;old=buttons;continue;
        }
        unsigned int movement=buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER);
        if(movement && catalog->count && (movement!=(old&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER)) || now>=repeat)) {
            int step=(movement&PSP_CTRL_LTRIGGER)?-8:(movement&PSP_CTRL_RTRIGGER)?8:(movement&PSP_CTRL_UP)?-1:1;
            selected=(selected+step+catalog->count*8)%catalog->count;
            repeat=now+((movement&~old)?400000ULL:180000ULL); dirty=1;
        }
        old=buttons; sceKernelDelayThread(20000);
    }
    free(catalog);
    return changed;
}
static int music_choose_preset(void) {return preset_choose(music_preset_file,1);}
static int music_load_selected(MdFileError *error) {
    char path[272]; snprintf(path,sizeof(path),"presets/%s",music_preset_file);
    return md_load_preset(path,&md_custom_preset,error);
}
#endif
