/* SPDX-License-Identifier: GPL-2.0-or-later */
static int auto_next=1,repeat_one,shuffle_music,show_spectrum=1,prefer_audio,prefer_subtitle;
static int spectrum_bands=24,spectrum_gain=2,spectrum_style,spectrum_segments=16;
static const char *language_codes[]={"", "en", "de", "ja", "fr", "es", "it"};
static const char *language_names[]={"Source default", "English", "German", "Japanese", "French", "Spanish", "Italian"};
static void preferences(int save){
    FILE *f=fopen(save?"D:\\preferences.tmp":"D:\\preferences.cfg",save?"w":"r");
    if(!f&&!save)f=fopen("D:\\preferences.bak","r");if(!f)return;
    if(save){
        int ok=fprintf(f,"auto_next=%d\nrepeat_one=%d\nshuffle_music=%d\nspectrum=%d\nquality=%d\nvolume=%d\naudio_language=%d\nsubtitle_language=%d\n",auto_next,repeat_one,shuffle_music,show_spectrum,quality,volume,prefer_audio,prefer_subtitle)>0;
        if(fprintf(f,"spectrum_bands=%d\nspectrum_gain=%d\nspectrum_style=%d\nspectrum_segments=%d\n",spectrum_bands,spectrum_gain,spectrum_style,spectrum_segments)<0)ok=0;
        if(fclose(f))ok=0;
        if(ok){
            remove("D:\\preferences.bak");
            MoveFileA("D:\\preferences.cfg","D:\\preferences.bak");
            if(!MoveFileA("D:\\preferences.tmp","D:\\preferences.cfg"))MoveFileA("D:\\preferences.bak","D:\\preferences.cfg");
        }
    }else{
        char line[100];while(fgets(line,sizeof(line),f)){char *eq=strchr(line,'=');if(!eq)continue;*eq++=0;int n=atoi(eq);
            if(!strcmp(line,"auto_next"))auto_next=!!n;else if(!strcmp(line,"repeat_one"))repeat_one=!!n;
            else if(!strcmp(line,"shuffle_music"))shuffle_music=!!n;else if(!strcmp(line,"spectrum"))show_spectrum=!!n;
            else if(!strcmp(line,"quality"))quality=!!n;else if(!strcmp(line,"volume")&&n>=0&&n<=100)volume=n;
            else if(!strcmp(line,"audio_language")&&n>=0&&n<7)prefer_audio=n;
            else if(!strcmp(line,"subtitle_language")&&n>=0&&n<7)prefer_subtitle=n;
            else if(!strcmp(line,"spectrum_bands")&&(n==12||n==24))spectrum_bands=n;
            else if(!strcmp(line,"spectrum_gain")&&n>=1&&n<=8)spectrum_gain=n;
            else if(!strcmp(line,"spectrum_style")&&n>=0&&n<=2)spectrum_style=n;
            else if(!strcmp(line,"spectrum_segments")&&n>=8&&n<=32)spectrum_segments=n;
        }fclose(f);
    }
}
