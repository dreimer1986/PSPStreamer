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
const char *tr(TextId id){(void)id;return "label";}
static void keep_awake(void){}
static void settings_shell(const char *s){(void)s;}
static void settings_line(int row,int selected,const char *s){(void)row;(void)selected;assert(s);}
static void settings_help(const char *s){(void)s;}
static void sceCtrlReadBufferPositive(SceCtrlData *p,int count){(void)count;assert(pos<n);p->Buttons=keys[pos++];}
static void sceKernelDelayThread(int us){tick+=us;}
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int settings_text(char *text,size_t cap,int secret,const char *label){(void)secret;(void)label;if(!input)return 0;snprintf(text,cap,"%s",input);return 1;}
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
    free(d.text);puts("Health rumble UI: hex editor, validation, defaults and plugin hub OK");
}
