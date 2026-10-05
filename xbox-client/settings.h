/* SPDX-License-Identifier: GPL-2.0-or-later */
static int auto_next=1,repeat_one,shuffle_music,show_spectrum=1,prefer_audio,prefer_subtitle;
static int next_delay;
static int output_selected_w,output_selected_h,output_selected_hz;
static const char *language_codes[]={"", "en", "de", "ja", "fr", "es", "it"};
static void preferences(int save){
    FILE *f=fopen(save?"D:\\preferences.tmp":"D:\\preferences.cfg",save?"w":"r");
    if(!f&&!save)f=fopen("D:\\preferences.bak","r");if(!f)return;
    if(save){
        int ok=fprintf(f,"auto_next=%d\nrepeat_one=%d\nshuffle_music=%d\nspectrum=%d\nquality=%d\nvolume=%d\naudio_language=%d\nsubtitle_language=%d\n",auto_next,repeat_one,shuffle_music,show_spectrum,quality,volume,prefer_audio,prefer_subtitle)>0;
        if(fprintf(f,"spectrum_analysis=%d\nspectrum_band_count=%d\nspectrum_gain_db=%d\nspectrum_style_v2=%d\nspectrum_segments_v2=%d\nspectrum_led_count=%d\nspectrum_peak_hold=%d\n",spectrum_analysis_mode,spectrum_band_count,spectrum_gain_db,spectrum_style,spectrum_segments,spectrum_led_count,spectrum_peak_hold)<0)ok=0;
        if(fprintf(f,"debug=%d\nnext_delay=%d\n",diagnostics_enabled,next_delay)<0)ok=0;
        if(fprintf(f,"video_codec=%d\ndisplay_wide=%d\noutput_width=%d\noutput_height=%d\noutput_hz=%d\n",video_codec,display_wide,output_selected_w,output_selected_h,output_selected_hz)<0)ok=0;
        if(fprintf(f,"audio_matrix=%d\n",audio_matrix)<0)ok=0;
        if(fprintf(f,"ui_language=%d\naudio_quality=%d\n",ui_language,audio_quality)<0)ok=0;
        if(fprintf(f,"video_hardware=%d\n",video_hardware)<0)ok=0;
        if(!visual_preferences_write(f))ok=0;
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
            else if(!strcmp(line,"quality")&&n>=0&&n<6)quality=n;else if(!strcmp(line,"volume")&&n>=0&&n<=100)volume=n;
            else if(!strcmp(line,"video_codec"))video_codec=!!n;
            else if(!strcmp(line,"video_hardware"))video_hardware=!!n;
            else if(!strcmp(line,"audio_matrix")&&n>=0&&n<3)audio_matrix=n;
            else if(!strcmp(line,"ui_language")&&n>=0&&n<3)ui_language=n;
            else if(!strcmp(line,"audio_quality")&&n>=0&&n<5)audio_quality=n;
            else if(!strcmp(line,"display_wide"))display_wide=!!n;
            else if(!strcmp(line,"output_width"))output_selected_w=n;
            else if(!strcmp(line,"output_height"))output_selected_h=n;
            else if(!strcmp(line,"output_hz"))output_selected_hz=n;
            else if(!strcmp(line,"audio_language")&&n>=0&&n<7)prefer_audio=n;
            else if(!strcmp(line,"subtitle_language")&&n>=0&&n<7)prefer_subtitle=n;
            else if((!strcmp(line,"spectrum_bands")||!strcmp(line,"spectrum_band_count"))&&spectrum_band_valid(n))spectrum_band_count=n;
            else if(!strcmp(line,"spectrum_analysis"))spectrum_analysis_mode=!!n;
            else if(!strcmp(line,"spectrum_gain_db")&&n>=-24&&n<=24)spectrum_gain_db=n;
            else if(!strcmp(line,"spectrum_style_v2")&&n>=0&&n<=4)spectrum_style=n;
            else if(!strcmp(line,"spectrum_segments_v2"))spectrum_segments=!!n;
            else if(!strcmp(line,"spectrum_led_count")&&n>=8&&n<=32)spectrum_led_count=n;
            else if(!strcmp(line,"spectrum_peak_hold"))spectrum_peak_hold=!!n;
            else if(!strcmp(line,"debug"))diagnostics_enabled=!!n;
            else if(!strcmp(line,"next_delay")&&n>=0&&n<=30)next_delay=n;
            else visual_preferences_read(line,eq);
        }fclose(f);
    }
}
