#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../psp-client/language.h"
typedef struct{unsigned Buttons;} SceCtrlData;
enum{PSP_CTRL_UP=1,PSP_CTRL_DOWN=2,PSP_CTRL_LEFT=4,PSP_CTRL_RIGHT=8,PSP_CTRL_CROSS=16,PSP_CTRL_CIRCLE=32,PSP_CTRL_START=64,PSP_CTRL_SQUARE=128,PSP_CTRL_TRIANGLE=256};
static unsigned keys[64];static int pos,n;static unsigned long long tick;
static const char *input;
static const char **inputs;static int input_pos;
const char *tr(TextId id){(void)id;return "label";}
static void keep_awake(void){}
static void settings_shell(const char *s){(void)s;}
static void settings_line(int row,int selected,const char *s){(void)row;(void)selected;assert(s);}
static void settings_help(const char *s){(void)s;}
static void sceCtrlReadBufferPositive(SceCtrlData *p,int count){(void)count;assert(pos<n);p->Buttons=keys[pos++];}
static void sceKernelDelayThread(int us){tick+=us;}
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int settings_text(char *text,size_t cap,int secret,const char *label){(void)secret;(void)label;const char *value=inputs?inputs[input_pos++]:input;if(!value)return 0;snprintf(text,cap,"%s",value);return 1;}
#include "../psp-client/plugin_settings_ui.h"
static void feed(const unsigned *p,int count){memcpy(keys,p,count*sizeof(*p));pos=0;n=count;tick=0;}
int main(void){
    PluginIni d;assert(!pi_open(&d,"/no-such-folder/test.ini",65536));
    strcpy(d.text,"[title:ULUS12345]\nenabled=1\naddress=0x08801000\nmaximum=240\n");d.length=strlen(d.text);
    assert(!plugin_rules_valid(&d,plugin_health,12));assert(pi_get(&d,0,"address",0)==0x08801000);
    char value[48];plugin_value(value,sizeof(value),plugin_health+1,0x08801000);assert(!strcmp(value,"0x08801000"));
    assert(!pi_set(&d,0,"address",1));assert(plugin_rules_valid(&d,plugin_health,12)<0);
    /* Enter the address field, accept hexadecimal input, return to list. */
    unsigned edit[]={0,PSP_CTRL_DOWN,0,PSP_CTRL_CROSS,0,PSP_CTRL_CIRCLE};
    input="0x08801000";feed(edit,sizeof(edit)/sizeof(*edit));plugin_fields(&d,0,plugin_health,12,"rumble");
    assert(pi_get(&d,0,"address",0)==0x08801000);assert(!plugin_rules_valid(&d,plugin_health,12));
    assert(!pi_set(&d,0,"minimum",241));assert(plugin_rules_valid(&d,plugin_health,12)<0);
    assert(!pi_set(&d,0,"minimum",0));assert(!pi_set(&d,0,"gate_address",0x08801001));assert(plugin_rules_valid(&d,plugin_health,12)<0);
    assert(!pi_set(&d,0,"gate_address",0));assert(!plugin_rules_valid(&d,plugin_health,12));
    assert(!pi_set(&d,0,"offset",4));assert(plugin_rules_valid(&d,plugin_health,12)<0);
    assert(!pi_set(&d,0,"pointer",1));assert(!plugin_rules_valid(&d,plugin_health,12));
    unsigned exit[]={0,PSP_CTRL_CIRCLE};feed(exit,2);plugin_settings();
    uint32_t a,v;int w;
    assert(!hi_cw("_L 0x203E364C 0x43700000",&a,&v,&w)&&a==0x08be364c&&w==3);
    assert(hi_maximum(v,4)==240);
    assert(!hi_cw("003E364C FF",&a,&v,&w)&&w==1);
    assert(!hi_cw("103E364C FFFF",&a,&v,&w)&&w==2);
    assert(hi_cw("603E364C 43700000",&a,&v,&w)<0);
    assert(hi_cw("203E364C 43700000\n_L 203E364C 43700000",&a,&v,&w)<0);
    assert(hi_cw("203E364D 43700000",&a,&v,&w)<0);
    assert(hi_cw("203E364C FFFFFFFFF",&a,&v,&w)<0);
    assert(hi_cw("003E364C 100",&a,&v,&w)<0);
    assert(!hi_retro("00BCFF34",&a)&&a==0x08bcff34);
    assert(hi_retro("08BCFF34",&a)<0);assert(hi_retro("0",&a)<0);
    assert(!hi_maximum(0x7f800001,4));assert(!hi_maximum(0xff800000,4));
    const char *cw[]={"_L 0x203E364C 0x43700000","4","240","ULES01298"};
    inputs=cw;input_pos=0;unsigned notice[]={0,PSP_CTRL_CIRCLE};feed(notice,2);
    assert(plugin_import_health(&d,0,1)==1);
    assert(pi_get(&d,1,"enabled",1)==0&&pi_get(&d,1,"address",0)==0x08be364c&&pi_get(&d,1,"type",0)==4);
    assert(pi_get(&d,1,"maximum",0)==240);
    size_t len=d.length;input_pos=0;feed(notice,2);assert(plugin_import_health(&d,0,2)<0&&d.length==len);
    const char *ra[]={"00BCFF34","3","240","ULES00001"};inputs=ra;input_pos=0;feed(notice,2);
    assert(plugin_import_health(&d,1,2)==2&&pi_get(&d,2,"address",0)==0x08bcff34);
    const char *cancel[]={"00BCFF34",NULL};inputs=cancel;input_pos=0;len=d.length;
    assert(plugin_import_health(&d,1,3)<0&&d.length==len);
    free(d.text);puts("Health rumble UI/import: formats, hex editor, validation, duplicates, cancel and disabled drafts OK");
}
