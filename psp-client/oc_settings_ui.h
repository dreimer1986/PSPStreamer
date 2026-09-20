#include "oc_settings_io.h"
static void oc_settings(void) {
    const char *path="ms0:/SEPLUGINS/StreamerOC/StreamerOC.ini";
    OcConfig draft;int loaded=oc_settings_read(path,&draft);
    if(loaded==1) {
        OcConfig internal;
        const char *alternate="ef0:/SEPLUGINS/StreamerOC/StreamerOC.ini";
        int alternate_loaded=oc_settings_read(alternate,&internal);
        if(alternate_loaded!=1){loaded=alternate_loaded;if(!loaded)draft=internal;path=alternate;}
    }
    if(loaded<0) {
        settings_shell(tr(TXT_OC_TITLE));settings_help(tr(TXT_OC_READ_FAILED));
        SceCtrlData pad;do{keep_awake();sceCtrlReadBufferPositive(&pad,1);sceKernelDelayThread(20000);}while(!(pad.Buttons&PSP_CTRL_CIRCLE));
        return;
    }
    int *values[]={&draft.enabled,&draft.target,&draft.enforce,&draft.enforce_unlimited,&draft.app_control,&draft.report,&draft.overlay};
    int selected=0,dirty=1;unsigned old=PSP_CTRL_CROSS;unsigned long long repeat=0;
    while(1) {
        keep_awake();SceCtrlData pad;sceCtrlReadBufferPositive(&pad,1);unsigned pressed=pad.Buttons&~old;
        if(dirty) {
            settings_shell(tr(TXT_OC_TITLE));
            for(int i=0;i<7;i++) {
                char row[96],value[24];
                if(i==1)snprintf(value,sizeof(value),"%d MHz",*values[i]);
                else if(i==6)snprintf(value,sizeof(value),"%s",tr(*values[i]==2?TXT_OC_HOOK:*values[i]==1?TXT_OC_POLLING:TXT_OFF));
                else snprintf(value,sizeof(value),"%s",tr(*values[i]?TXT_SETTINGS_ON:TXT_OFF));
                snprintf(row,sizeof(row),"%c %s: %s",i==selected?'>':' ',tr((TextId)(TXT_OC_ENABLED+i)),value);
                settings_line(i,i==selected,row);
            }
            settings_line(7,0,path);settings_line(8,0,tr(TXT_OC_WARNING));settings_help(tr(TXT_OC_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE)break;
        if(pressed&PSP_CTRL_START) {
            int result=oc_settings_write(path,&draft);
            snprintf(status,sizeof(status),"%s",tr(result?TXT_SETTINGS_FAILED:TXT_OC_SAVED));
            if(!result)break;
            settings_help(status);sceKernelDelayThread(1000000);dirty=1;
        }
        unsigned movement=pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(movement && ((pressed&movement)||now>=repeat)) {
            if(movement&PSP_CTRL_UP)selected=(selected+6)%7;
            else if(movement&PSP_CTRL_DOWN)selected=(selected+1)%7;
            else if(selected==1) {
                int value=*values[1]+((movement&PSP_CTRL_LEFT)?-1:1);
                if(value>=66 && value<=471)*values[1]=value;
            } else if(selected==6)*values[selected]=(*values[selected]+((movement&PSP_CTRL_LEFT)?2:1))%3;
            else *values[selected]=!*values[selected];
            repeat=now+((pressed&movement)?400000ULL:150000ULL);dirty=1;
        }
        old=pad.Buttons;sceKernelDelayThread(20000);
    }
}
