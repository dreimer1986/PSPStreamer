/* Idle-menu conveniences; all persistence runs outside media workers. */
static void comfort_notice(const char *text) {
    settings_shell(tr(TXT_COMFORT));settings_line(2,0,text);
    settings_help(tr(TXT_DOWNLOAD_BACK));
    unsigned int old=~0U;
    while(1){SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons&~old)&(PSP_CTRL_CROSS|PSP_CTRL_CIRCLE))break;
        old=pad.Buttons;sceKernelDelayThread(20000);}
}
static int comfort_resume_prompt(const char *id,int local) {
    char scope[80];comfort_scope(scope,local);
    int index=comfort_find(&comfort_store,scope,id,0);
    int seconds=index>=0?comfort_store.records[index].seconds:0;
    /* Provider is authoritative, including a cleared/completed position. */
    if(!local && provider_resume_seconds>=0)seconds=provider_resume_seconds;
    if(seconds<5 || seconds>86400 || (current_duration_seconds>0 && seconds>=current_duration_seconds-3))return 1;
    char line[96];snprintf(line,sizeof(line),tr(TXT_RESUME_QUESTION),seconds/60,seconds%60);
    settings_shell(tr(TXT_VIDEO));settings_line(1,0,current_media_name);
    settings_line(3,0,line);settings_help(tr(TXT_RESUME_HELP));
    unsigned int old=~0U;
    while(1) {
        SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);
        unsigned int pressed=pad.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)return 0;
        if(pressed&PSP_CTRL_CROSS){stream_start_seconds=seconds;return 1;}
        if(pressed&PSP_CTRL_SQUARE){stream_start_seconds=0;return 1;}
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
static int comfort_shortcuts(int favorites) {
    int selected=0,dirty=1;unsigned int old=~0U;unsigned long long repeat=0;
    char scope[80];comfort_scope(scope,0);
    while(1) {
        int order[COMFORT_RECORDS],count=0;
        for(int i=0;i<COMFORT_RECORDS;i++) {
            ComfortRecord *r=&comfort_store.records[i];
            if(!r->id[0] || (strcmp(r->scope,scope)&&strcmp(r->scope,"local")) ||
               (favorites?!r->favorite:(!r->used||r->folder)))continue;
            int j=count++;while(j>0 && comfort_store.records[order[j-1]].used<r->used){order[j]=order[j-1];j--;}
            order[j]=i;
        }
        if(selected>=count)selected=count?count-1:0;
        if(dirty) {
            settings_shell(tr(favorites?TXT_FAVORITES:TXT_RECENT));
            if(!count)settings_line(0,0,tr(TXT_NO_ENTRIES));
            for(int i=selected/8*8;i<count && i<selected/8*8+8;i++)settings_line(i%8,i==selected,comfort_store.records[order[i]].name);
            settings_help(tr(TXT_SHORTCUT_HELP));dirty=0;
        }
        SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);unsigned int pressed=pad.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)return 0;
        if(count && (pressed&PSP_CTRL_TRIANGLE)) {
            ComfortRecord *r=&comfort_store.records[order[selected]];
            if(favorites)r->favorite=0;else r->used=0;
            if(!comfort_save_file(COMFORT_PATH,&comfort_store))comfort_notice(tr(TXT_SETTINGS_FAILED));
            dirty=1;
        }
        if(count && (pressed&PSP_CTRL_CROSS)) {
            ComfortRecord *r=&comfort_store.records[order[selected]];
            memset(&comfort_shortcut,0,sizeof(comfort_shortcut));
            snprintf(comfort_shortcut.title,TITLE_SIZE,"%s",r->name);
            snprintf(comfort_shortcut.value,ID_SIZE,"%s",r->id);
            comfort_shortcut.is_folder=!strcmp(r->scope,"local")?3:r->folder;
            comfort_shortcut.is_audio=r->audio;comfort_shortcut_pending=1;return 1;
        }
        unsigned int direction=pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(count && direction && ((pressed&direction)||now>=repeat)) {
            selected=(selected+(direction&PSP_CTRL_DOWN?1:count-1))%count;
            repeat=now+((pressed&direction)?400000:150000);dirty=1;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
static int comfort_profiles(void) {
    int selected=0,dirty=1;unsigned int old=~0U;
    while(1) {
        if(dirty) {
            settings_shell(tr(TXT_SERVER_PROFILES));
            for(int i=0;i<COMFORT_PROFILES;i++)settings_line(i,i==selected,comfort_store.profiles[i].host[0]?comfort_store.profiles[i].name:tr(TXT_EMPTY_SLOT));
            settings_line(7,0,tr(TXT_PROFILE_SAVED));settings_help(tr(TXT_PROFILE_HELP));dirty=0;
        }
        SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);unsigned int pressed=pad.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)return 0;
        if(pressed&PSP_CTRL_DOWN){selected=(selected+1)%COMFORT_PROFILES;dirty=1;}
        if(pressed&PSP_CTRL_UP){selected=(selected+COMFORT_PROFILES-1)%COMFORT_PROFILES;dirty=1;}
        ComfortProfile *p=&comfort_store.profiles[selected];
        if(pressed&PSP_CTRL_SQUARE) {
            ComfortProfile draft={0};snprintf(draft.name,sizeof(draft.name),"%.31s",server_host);
            if(settings_text(draft.name,sizeof(draft.name),0,tr(TXT_PROFILE_NAME)) && draft.name[0]) {
                strcpy(draft.host,server_host);strcpy(draft.password,server_password);draft.port=server_port;draft.https=server_https;
                *p=draft;
                if(!comfort_save_file(COMFORT_PATH,&comfort_store))comfort_notice(tr(TXT_SETTINGS_FAILED));
            }
            memset(&draft,0,sizeof(draft));dirty=1;sceCtrlReadBufferPositive(&pad,1);
        }
        if((pressed&PSP_CTRL_TRIANGLE)&&p->host[0]) {
            memset(p,0,sizeof(*p));
            if(!comfort_save_file(COMFORT_PATH,&comfort_store))comfort_notice(tr(TXT_SETTINGS_FAILED));
            dirty=1;
        }
        if((pressed&PSP_CTRL_CROSS)&&p->host[0]) {
            AppSettings original;settings_capture(&original);
            strcpy(server_host,p->host);strcpy(server_password,p->password);server_port=p->port;server_https=p->https;
            server_auth_update();
            if(save_playback_settings()<0) {
                settings_apply(&original);comfort_notice(tr(TXT_SETTINGS_FAILED));dirty=1;
            } else {
                have_cached_server_address=0;resume_pending=0;remote_control_sequence=0;
                remote_session[0]=0;current_path[0]=current_parent_path[0]=0;item_count=0;
                tls_notice_clear();menu_art_select("");memset(&original,0,sizeof(original));return 1;
            }
            memset(&original,0,sizeof(original));
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
static int comfort_menu(void) {
    int selected=0,dirty=1;unsigned int old=~0U;
    while(1) {
        if(dirty) {
            settings_shell(tr(TXT_COMFORT));
            const TextId labels[]={TXT_FAVORITES,TXT_RECENT,TXT_FAVORITE_TOGGLE,TXT_SERVER_PROFILES};
            for(int i=0;i<4;i++)settings_line(i,selected==i,tr(labels[i]));
            unsigned long long now=sceKernelGetSystemTimeWide();
            int minutes=comfort_deadline>now?(comfort_deadline-now+59999999ULL)/60000000ULL:0;
            char line[96];snprintf(line,sizeof(line),tr(TXT_TIMER),minutes);settings_line(4,selected==4,line);
            snprintf(line,sizeof(line),tr(TXT_STOP_AFTER),comfort_remaining);settings_line(5,selected==5,line);
            settings_line(7,0,tr(TXT_TIMER_HINT));settings_help(tr(TXT_COMFORT_HELP));dirty=0;
        }
        SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);unsigned int pressed=pad.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)return 0;
        if(pressed&PSP_CTRL_DOWN){selected=(selected+1)%6;dirty=1;}
        if(pressed&PSP_CTRL_UP){selected=(selected+5)%6;dirty=1;}
        if(pressed&PSP_CTRL_CROSS) {
            if(selected<2 && comfort_shortcuts(selected==0))return 1;
            if(selected==3 && comfort_profiles())return 1;
            if(selected==2 && comfort_focus.value[0] && comfort_focus.is_folder<2) {
                char scope[80];comfort_scope(scope,0);
                int i=comfort_find(&comfort_store,scope,comfort_focus.value,1);
                if(i<0)comfort_notice(tr(TXT_STORAGE_FULL));
                else {
                    ComfortRecord *r=&comfort_store.records[i];
                    snprintf(r->name,sizeof(r->name),"%s",comfort_focus.title);
                    r->folder=comfort_focus.is_folder;r->audio=comfort_focus.is_audio;r->favorite=!r->favorite;
                    comfort_notice(tr(comfort_save_file(COMFORT_PATH,&comfort_store)?TXT_COMFORT_SAVED:TXT_SETTINGS_FAILED));
                }
            }
            dirty=1;sceCtrlReadBufferPositive(&pad,1);
        }
        if(pressed&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT)) {
            int delta=pressed&PSP_CTRL_LEFT?-1:1;
            if(selected==4) {
                unsigned long long now=sceKernelGetSystemTimeWide();
                int minutes=comfort_deadline>now?(comfort_deadline-now+59999999ULL)/60000000ULL:0;
                minutes=((minutes+14)/15*15+delta*15+135)%135;
                comfort_deadline=minutes?now+(unsigned long long)minutes*60000000ULL:0;
            }
            if(selected==5)comfort_remaining=(comfort_remaining+delta+11)%11;
            dirty=1;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
