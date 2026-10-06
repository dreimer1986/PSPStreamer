#include "plugin_settings_io.h"
#include "health_import.h"
#include "../psp-overclock/config_parse.h"
typedef struct {const char *key;TextId label;int min,max,def;} PluginField;
static const PluginField plugin_oc[]={
    {"enabled",TXT_OC_ENABLED,0,1,0},{"target_mhz",TXT_OC_TARGET,66,500,333},
    {"enforce",TXT_OC_ENFORCE,0,1,0},{"enforce_unlimited",TXT_OC_UNLIMITED,0,1,0},
    {"app_control",TXT_OC_APP_CONTROL,0,1,1},{"report",TXT_OC_REPORT,0,1,1},
    {"overlay",TXT_OC_OVERLAY,0,2,1},{"overlay_always",TXT_PLUGIN_ALWAYS,0,1,0}
};
static const PluginField plugin_pad[]={
    {"enabled",TXT_OC_ENABLED,0,1,1},{"home_combo",TXT_PLUGIN_HOME,0,1,1},
    {"overlay",TXT_OC_OVERLAY,0,2,1},{"overlay_always",TXT_PLUGIN_ALWAYS,0,1,0},
    {"tvout",TXT_PLUGIN_TV,0,2,0},{"metadata",TXT_PLUGIN_METADATA,0,1,0},
    {"pops_rumble",TXT_PLUGIN_RUMBLE,0,1,0},{"report",TXT_OC_REPORT,0,1,1},
    {"vsh",TXT_PLUGIN_VSH,0,1,1},{"pops",TXT_PLUGIN_POPS,0,1,1}
};
static const PluginField plugin_fusa[]={
    {"auto_zoom",TXT_PLUGIN_AUTOZOOM,0,1,0},{"auto_zoom_delay_seconds",TXT_PLUGIN_DELAY,1,60,5},
    {"keep_fullscreen",TXT_PLUGIN_KEEP,0,1,1},{"experimental_speedboost",TXT_PLUGIN_GUARD,0,1,0}
};
static const PluginField plugin_health[]={
    {"enabled",TXT_OC_ENABLED,0,1,0},{"address",TXT_HR_ADDRESS,0,0x09ffffff,0},
    {"type",TXT_HR_TYPE,1,4,2},{"pointer",TXT_HR_POINTER,0,1,0},
    {"offset",TXT_HR_OFFSET,0,65535,0},{"minimum",TXT_HR_MIN,0,2147483647,0},
    {"maximum",TXT_HR_MAX,1,2147483647,100},{"strength",TXT_HR_STRENGTH,0,255,180},
    {"duration_ms",TXT_HR_DURATION,10,1000,120},{"cooldown_ms",TXT_HR_COOLDOWN,20,5000,200},
    {"gate_address",TXT_HR_GATE,0,0x09ffffff,0},{"gate_value",TXT_HR_GATE_VALUE,0,2147483647,1},
    {"dynamic",TXT_HR_MODE,0,1,0},{"damage_peak",TXT_HR_PEAK,1,2147483647,120},
    {"duration_min_ms",TXT_HR_DURATION_MIN,10,1000,60},{"duration_max_ms",TXT_HR_DURATION_MAX,10,1000,500}
};
static int plugin_hex(const PluginField *f){return strstr(f->key,"address")!=NULL || !strcmp(f->key,"offset");}
static TextId plugin_health_hint(const char *key){
    if(!strcmp(key,"dynamic")||!strcmp(key,"damage_peak"))return TXT_HR_DYNAMIC_HINT;
    if(strstr(key,"address"))return !strcmp(key,"address")?TXT_HR_ADDRESS_HINT:TXT_HR_GATE_HINT;
    if(!strcmp(key,"type"))return TXT_HR_TYPE_HINT;
    if(!strcmp(key,"pointer")||!strcmp(key,"offset"))return TXT_HR_POINTER_HINT;
    if(!strcmp(key,"minimum")||!strcmp(key,"maximum"))return TXT_HR_RANGE_HINT;
    if(!strcmp(key,"cooldown_ms"))return TXT_HR_COOLDOWN_HINT;
    return TXT_HR_DEFAULT_HINT;
}
static int plugin_global_valid(PluginIni *d,const PluginField *fields,int count) {
    if(fields==plugin_oc){char *copy=malloc(d->length+1);if(!copy)return -1;
        memcpy(copy,d->text,d->length+1);OcConfig config;int keys,line;
        int rc=oc_config_parse(copy,(int)d->length,&config,&keys,&line);free(copy);return rc;}
    for(size_t p=0;p<d->length;p=pi_next(d,p)){char line[384];if(pi_line(d,p,line,sizeof(line)))return -1;
        for(int i=0;i<count;i++){const char *v;if(!pi_key(line,fields[i].key,&v))continue;
            char *end;long number=strtol(v,&end,10);if(end==v)return -1;
            while(*end==' '||*end=='\t')end++;
            if(*end||number<fields[i].min||number>fields[i].max)return -1;
        }
    }return 0;
}
static void plugin_notice(TextId id) {
    settings_shell(tr(TXT_PLUGINS));settings_line(0,0,tr(id));settings_help(tr(TXT_PLUGIN_HELP));
    SceCtrlData p;do {keep_awake();sceCtrlReadBufferPositive(&p,1);sceKernelDelayThread(20000);}while(p.Buttons);
    do {keep_awake();sceCtrlReadBufferPositive(&p,1);sceKernelDelayThread(20000);}while(!(p.Buttons&PSP_CTRL_CIRCLE));
}
/* Shared edge/repeat handling: never reuse the X that opened a child menu. */
typedef struct {unsigned old;unsigned long long repeat;} PluginKeys;
static void plugin_keys_reset(PluginKeys *k) {SceCtrlData p;sceCtrlReadBufferPositive(&p,1);k->old=p.Buttons;k->repeat=0;}
static unsigned plugin_keys(PluginKeys *k) {
    SceCtrlData p;keep_awake();sceCtrlReadBufferPositive(&p,1);
    unsigned hit=p.Buttons&~k->old,m=p.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(m&&((hit&m)||now>=k->repeat)){k->repeat=now+((hit&m)?400000:150000);hit|=m;}
    k->old=p.Buttons;sceKernelDelayThread(20000);return hit;
}
static void plugin_value(char *out,size_t cap,const PluginField *f,int v) {
    if(v<0)snprintf(out,cap,"%s",tr(TXT_PLUGIN_INHERIT));
    else if(f->label==TXT_HR_MODE)snprintf(out,cap,"%s",tr(v?TXT_HR_DYNAMIC:TXT_HR_FIXED));
    else if(plugin_hex(f))snprintf(out,cap,"0x%08X",(unsigned)v);
    else if(f->label==TXT_HR_TYPE)snprintf(out,cap,"%s",v==1?"uint8":v==2?"uint16":v==3?"uint32":"float32");
    else if(!strcmp(f->key,"overlay"))snprintf(out,cap,"%s",tr(v==2?TXT_PLUGIN_HOOK:v==1?TXT_PLUGIN_POLL:TXT_PLUGIN_OFF));
    else if(!strcmp(f->key,"tvout"))snprintf(out,cap,"%s",tr(v==2?TXT_PLUGIN_START:v==1?TXT_PLUGIN_CONNECTED:TXT_PLUGIN_OFF));
    else if(f->max==1)snprintf(out,cap,"%s",tr(v?TXT_PLUGIN_ON:TXT_PLUGIN_OFF));
    else snprintf(out,cap,"%d%s",v,!strcmp(f->key,"target_mhz")?" MHz":"");
}
static int plugin_fields(PluginIni *d,int section,const PluginField *fields,int count,const char *title) {
    int sel=0,dirty=1;PluginKeys k;plugin_keys_reset(&k);
    while(1){
        if(dirty){settings_shell(title);int first=sel/7*7;
            for(int i=first;i<count&&i<first+7;i++){
                char value[48],row[128];plugin_value(value,sizeof(value),fields+i,pi_get(d,section,fields[i].key,(section<0||fields==plugin_health)?fields[i].def:-1));
                snprintf(row,sizeof(row),"%s: %s",tr(fields[i].label),value);settings_line(i-first,i==sel,row);
            }
            settings_line(8,0,tr(fields==plugin_health?plugin_health_hint(fields[sel].key):!strcmp(fields[sel].key,"target_mhz")?TXT_OC_WARNING:section<0?TXT_PLUGIN_MAIN_HINT:TXT_PLUGIN_PATH_HINT));
            settings_help(tr(section<0?TXT_PLUGIN_HELP:TXT_PLUGIN_VALUES_HELP));dirty=0;
        }
        unsigned hit=plugin_keys(&k);if(hit&PSP_CTRL_CIRCLE)return 0;
        if(section<0&&(hit&PSP_CTRL_START)){if(!plugin_global_valid(d,fields,count)&&!pi_save(d)){plugin_notice(TXT_PLUGIN_RESTART);return 1;}plugin_notice(TXT_PLUGIN_ERROR);plugin_keys_reset(&k);dirty=1;}
        if(hit&PSP_CTRL_UP){sel=(sel+count-1)%count;dirty=1;}
        else if(hit&PSP_CTRL_DOWN){sel=(sel+1)%count;dirty=1;}
        else if(hit&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT|PSP_CTRL_CROSS|PSP_CTRL_SQUARE)){
            const PluginField *f=fields+sel;int v=pi_get(d,section,f->key,(section<0||fields==plugin_health)?f->def:-1);
            if(section>=0&&(hit&PSP_CTRL_SQUARE))v=-1;
            else if((hit&PSP_CTRL_CROSS)&&f->max>2){
                char text[24];if(plugin_hex(f))snprintf(text,sizeof(text),"0x%08X",(unsigned)(v<0?f->def:v));else snprintf(text,sizeof(text),"%d",v<0?f->def:v);
                if(!settings_text(text,sizeof(text),0,tr(f->label))){plugin_keys_reset(&k);dirty=1;continue;}
                char *end,*start=text;while(*start==' '||*start=='\t')start++;
                errno=0;long number=strtol(start,&end,plugin_hex(f)&&start[0]=='0'&&(start[1]=='x'||start[1]=='X')?16:10);
                if(errno||end==start||*end||number<f->min||number>f->max){plugin_notice(TXT_PLUGIN_ERROR);plugin_keys_reset(&k);dirty=1;continue;}
                v=(int)number;plugin_keys_reset(&k);
            }else if(hit&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT|PSP_CTRL_CROSS)){
                if(v<0)v=f->def;else if(hit&PSP_CTRL_LEFT){if(v>f->min || f->max<=2)v--;}else if(v<f->max || f->max<=2)v++;
                if(v<f->min)v=f->max>2?f->min:f->max;
                if(v>f->max)v=f->max>2?f->max:f->min;
            }else continue;
            if(pi_set(d,section,f->key,v)){plugin_notice(TXT_PLUGIN_ERROR);plugin_keys_reset(&k);}dirty=1;
        }
    }
}
static int plugin_rules_valid(PluginIni *d,const PluginField *fields,int count) {
    TitleRuleKey keys[TITLE_RULE_MAX_KEYS];for(int i=0;i<count;i++){keys[i].name=fields[i].key;keys[i].minimum=fields[i].min;keys[i].maximum=fields[i].max;}
    if(pi_validate_rules(d,keys,count))return -1;
    if(fields==plugin_health){size_t a,b;
        for(int s=0;!pi_section(d,s,&a,&b,NULL);s++){
            int c[16];for(int i=0;i<16;i++)c[i]=pi_get(d,s,fields[i].key,fields[i].def);
            if(c[12]&&c[14]>c[15])return -1;
            if(!c[0])continue;
            unsigned bytes=c[3]?4:c[2]==1?1:c[2]==2?2:4;
            if(c[1]<0x08800000||c[1]>0x0a000000-(int)bytes||(c[1]&(bytes-1))||c[5]>=c[6]||(!c[3]&&c[4]))return -1;
            if(c[10]&&(c[10]<0x08800000||c[10]>0x09fffffc||(c[10]&3)))return -1;
        }
    }return 0;
}
static int plugin_rule_name(char *name,int kind) {
    if(!settings_text(name,256,0,tr(kind?TXT_PLUGIN_ADD_PATH:TXT_PLUGIN_ADD_TITLE)))return 0;
    if(!name[0]||strpbrk(name,"[]\r\n"))return -1;
    if(!kind){int n=0;for(char *p=name;*p;p++){if(*p=='-')continue;if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')))return -1;n++;}if(n!=9)return -1;}
    return 1;
}
static int plugin_import_health(PluginIni *d,int retro,int sections){
    char text[96]={0},name[256]={0};uint32_t address=0,value=0;int width=3,type,maximum;
    if(!settings_text(text,sizeof(text),0,tr(retro?TXT_HI_RETRO_INPUT:TXT_HI_CW_INPUT)))return -1;
    if(retro?hi_retro(text,&address):hi_cw(text,&address,&value,&width))goto invalid;
    /* 32-bit writes do not identify float versus integer. Always ask. */
    snprintf(text,sizeof(text),"%d",width);
    if(!settings_text(text,sizeof(text),0,tr(TXT_HI_TYPE)))return -1;
    char *end;errno=0;long v=strtol(text,&end,10);
    if(errno||end==text||*end||v<1||v>4)goto invalid;
    type=(int)v;
    if(!hi_address(address,type)||(!retro&&type!=width&&!(width==3&&type==4)))goto invalid;
    maximum=retro?0:hi_maximum(value,type);
    if(maximum)snprintf(text,sizeof(text),"%d",maximum);else text[0]=0;
    if(!settings_text(text,sizeof(text),0,tr(TXT_HI_MAX)))return -1;
    errno=0;v=strtol(text,&end,10);
    if(errno||end==text||*end||v<1||v>INT_MAX)goto invalid;
    maximum=(int)v;
    int rc=plugin_rule_name(name,0);if(!rc)return -1;if(rc<0)goto invalid;
    for(int i=0;i<sections;i++){size_t a,b;char header[384];pi_section(d,i,&a,&b,header);
        if(!strncmp(header,"[title:",7)){char *e=strrchr(header,']');if(e)*e=0;
            if(title_rule_equal(header+7,name,1)){plugin_notice(TXT_HI_DUPLICATE);return -1;}}}
    char row[512];snprintf(row,sizeof(row),"%s[title:%s]\nenabled=0\naddress=0x%08X\ntype=%d\nminimum=0\nmaximum=%d\n",
        d->length&&d->text[d->length-1]!='\n'?"\n":"",name,(unsigned)address,type,maximum);
    if(pi_replace(d,d->length,d->length,row))goto invalid;
    plugin_notice(TXT_HI_REVIEW);return sections;
invalid:plugin_notice(TXT_PLUGIN_ERROR);return -1;
}
static void plugin_rules(PluginIni *d,const PluginField *fields,int count,const char *title) {
    /* Structurally valid profiles remain editable even if their enabled
     * address/range needs repairing. Semantic checks apply when saving. */
    TitleRuleKey keys[TITLE_RULE_MAX_KEYS];for(int i=0;i<count;i++){keys[i].name=fields[i].key;keys[i].minimum=fields[i].min;keys[i].maximum=fields[i].max;}
    if(pi_validate_rules(d,keys,count)){plugin_notice(TXT_PLUGIN_ERROR);return;}
    int sel=0,dirty=1,confirm=-1,actions=fields==plugin_health?4:2;PluginKeys k;plugin_keys_reset(&k);
    while(1){
        int sections=0;size_t a,b;while(!pi_section(d,sections,&a,&b,NULL))sections++;
        int total=sections+actions;if(sel>=total)sel=total-1;
        if(dirty){settings_shell(title);int first=sel/7*7;
            for(int i=first;i<total&&i<first+7;i++){char name[384];
                if(i<2)snprintf(name,sizeof(name),"%s",tr(i?TXT_PLUGIN_ADD_PATH:TXT_PLUGIN_ADD_TITLE));
                else if(i<actions)snprintf(name,sizeof(name),"%s",tr(i==2?TXT_HI_CW:TXT_HI_RETRO));
                else pi_section(d,i-actions,&a,&b,name);
                char display[56];snprintf(display,sizeof(display),"%.48s",name);settings_line(i-first,i==sel,display);
            }
            settings_line(8,0,tr(TXT_PLUGIN_HELP));settings_help(tr(confirm>=0?TXT_PLUGIN_CONFIRM:TXT_PLUGIN_RULE_HELP));dirty=0;
        }
        unsigned hit=plugin_keys(&k);if(hit&PSP_CTRL_CIRCLE)return;
        if(hit&PSP_CTRL_START){if(!plugin_rules_valid(d,fields,count)&&!pi_save(d)){plugin_notice(TXT_PLUGIN_RESTART);return;}plugin_notice(TXT_PLUGIN_ERROR);plugin_keys_reset(&k);dirty=1;}
        if(hit&PSP_CTRL_UP){sel=(sel+total-1)%total;confirm=-1;dirty=1;}
        else if(hit&PSP_CTRL_DOWN){sel=(sel+1)%total;confirm=-1;dirty=1;}
        else if(sel>=actions&&(hit&PSP_CTRL_TRIANGLE)){
            if(confirm==sel){pi_section(d,sel-actions,&a,&b,NULL);pi_replace(d,a,b,"");confirm=-1;}else confirm=sel;dirty=1;
        }else if(hit&(PSP_CTRL_CROSS|PSP_CTRL_SQUARE)){
            confirm=-1;
            if(sel>=2&&sel<actions){
                if(hit&PSP_CTRL_CROSS){int added=plugin_import_health(d,sel==3,sections);if(added>=0)sel=added+actions;}
            }
            else if(sel>=actions&&(hit&PSP_CTRL_CROSS))plugin_fields(d,sel-actions,fields,count,title);
            else {
                char name[256]={0},header[384];int kind=sel==1;
                if(sel>=actions){pi_section(d,sel-actions,&a,&b,header);kind=!strncmp(header,"[path:",6);char *end=strrchr(header,']');if(end)*end=0;
                    if(strlen(header+(kind?6:7))>=sizeof(name)){plugin_notice(TXT_PLUGIN_ERROR);plugin_keys_reset(&k);dirty=1;continue;}
                    snprintf(name,sizeof(name),"%s",header+(kind?6:7));}
                int rc=plugin_rule_name(name,kind);
                if(rc>0){snprintf(header,sizeof(header),"%s[%s:%s]\n",sel<2&&d->length&&d->text[d->length-1]!='\n'?"\n":"",kind?"path":"title",name);
                    if(sel>=actions){pi_section(d,sel-actions,&a,&b,NULL);rc=pi_replace(d,a,pi_next(d,a),header);}else rc=pi_replace(d,d->length,d->length,header);
                    if(rc)plugin_notice(TXT_PLUGIN_ERROR);else if(sel<2)sel=sections+actions;
                }else if(rc<0)plugin_notice(TXT_PLUGIN_ERROR);
            }
            plugin_keys_reset(&k);dirty=1;
        }
    }
}
/* Filters retain their exact case-sensitive substring semantics. */
static void plugin_filters(PluginIni *d) {
    int sel=0,dirty=1,confirm=-1;PluginKeys k;plugin_keys_reset(&k);
    while(1){size_t pos[16];int type[16],counts[2]={0},n=0;
        for(size_t p=0;p<d->length;p=pi_next(d,p)){char s[384];const char *v;if(pi_line(d,p,s,sizeof(s)))continue;
            int t=pi_key(s,"allow_path",&v)?1:pi_key(s,"exclude_path",&v)?2:0;
            if(t){if(n==16||++counts[t-1]>8){plugin_notice(TXT_PLUGIN_ERROR);return;}pos[n]=p;type[n++]=t-1;}}
        if(sel>=n+2)sel=n+1;
        if(dirty){settings_shell(tr(TXT_PLUGIN_FILTERS));int first=sel/7*7;
            for(int i=first;i<n+2&&i<first+7;i++){char row[384];if(i<2)snprintf(row,sizeof(row),"%s",tr(i?TXT_PLUGIN_EXCLUDE:TXT_PLUGIN_ALLOW));else pi_line(d,pos[i-2],row,sizeof(row));
                char display[56];snprintf(display,sizeof(display),"%.48s",row);settings_line(i-first,i==sel,display);}
            settings_line(8,0,tr(TXT_PLUGIN_FILTER_HINT));settings_help(tr(confirm>=0?TXT_PLUGIN_CONFIRM:TXT_PLUGIN_HELP));dirty=0;}
        unsigned hit=plugin_keys(&k);if(hit&PSP_CTRL_CIRCLE)return;
        if(hit&PSP_CTRL_START){if(!pi_save(d)){plugin_notice(TXT_PLUGIN_RESTART);return;}plugin_notice(TXT_PLUGIN_ERROR);plugin_keys_reset(&k);dirty=1;}
        if(hit&PSP_CTRL_UP){sel=(sel+n+1)%(n+2);confirm=-1;dirty=1;}
        else if(hit&PSP_CTRL_DOWN){sel=(sel+1)%(n+2);confirm=-1;dirty=1;}
        else if(sel>=2&&(hit&PSP_CTRL_TRIANGLE)){if(confirm==sel){pi_replace(d,pos[sel-2],pi_next(d,pos[sel-2]),"");confirm=-1;}else confirm=sel;dirty=1;}
        else if(hit&PSP_CTRL_CROSS){char value[96]={0},row[384];int t=sel<2?sel:type[sel-2];
            if(sel>=2){const char *v;pi_line(d,pos[sel-2],row,sizeof(row));if(pi_key(row,t?"exclude_path":"allow_path",&v))snprintf(value,sizeof(value),"%s",v);}
            if(sel<2&&counts[t]>=8)plugin_notice(TXT_PLUGIN_ERROR);
            else if(settings_text(value,sizeof(value),0,tr(t?TXT_PLUGIN_EXCLUDE:TXT_PLUGIN_ALLOW))){
                if(!value[0]||strpbrk(value,"\r\n"))plugin_notice(TXT_PLUGIN_ERROR);
                else {snprintf(row,sizeof(row),"%s%s=%s\n",sel<2&&d->length&&d->text[d->length-1]!='\n'?"\n":"",t?"exclude_path":"allow_path",value);
                    size_t p=sel<2?d->length:pos[sel-2];if(pi_replace(d,p,sel<2?p:pi_next(d,p),row))plugin_notice(TXT_PLUGIN_ERROR);}}
            plugin_keys_reset(&k);dirty=1;confirm=-1;
        }
    }
}
static void plugin_settings(void) {
    const char *names[]={"StreamerOC","PSPConsolizer","FuSaFullscreen"};
    const PluginField *fields[]={plugin_oc,plugin_pad,plugin_fusa};
    const int counts[]={8,10,4},rules[]={5,7,0};
    int sel=0,dirty=1;PluginKeys k;plugin_keys_reset(&k);
    /* Flattened hub keeps all destinations directly reachable. */
    const int owner[]={0,0,1,1,1,2,1},kind[]={0,1,0,1,2,0,3};
    while(1){
        if(dirty){settings_shell(tr(TXT_PLUGINS));for(int i=0;i<7;i++){char row[96];snprintf(row,sizeof(row),"%s: %s",names[owner[i]],tr(kind[i]==3?TXT_HR_TITLE:kind[i]==1?TXT_PLUGIN_RULES:kind[i]==2?TXT_PLUGIN_FILTERS:TXT_PLUGIN_GLOBAL));settings_line(i,i==sel,row);}
            settings_line(8,0,tr(TXT_PLUGIN_MAIN_HINT));settings_help(tr(TXT_PLUGIN_HELP));dirty=0;}
        unsigned hit=plugin_keys(&k);if(hit&PSP_CTRL_CIRCLE)return;
        if(hit&PSP_CTRL_UP){sel=(sel+6)%7;dirty=1;}
        else if(hit&PSP_CTRL_DOWN){sel=(sel+1)%7;dirty=1;}
        else if(hit&PSP_CTRL_CROSS){int o=owner[sel],r=kind[sel];char path[192];
            snprintf(path,sizeof(path),"ms0:/SEPLUGINS/%s/%s%s.ini",names[o],names[o],r==3?"-rumble":r==1?"-rules":"");
            /* Plugins must already be installed; never silently create a
             * disabled plugin or change ARK's registrations. OC alone has ef0 support. */
            char mainpath[192];snprintf(mainpath,sizeof(mainpath),"ms0:/SEPLUGINS/%s/%s.ini",names[o],names[o]);
            FILE *f=fopen(mainpath,"rb");
            if(!f&&o==0&&errno==ENOENT){memcpy(mainpath,"ef0",3);f=fopen(mainpath,"rb");if(f)memcpy(path,"ef0",3);}
            if(!f)plugin_notice(TXT_PLUGIN_ERROR);
            else {fclose(f);PluginIni d;if(pi_open(&d,path,(r==1||r==3)?65536:o==2?1023:2047))plugin_notice(TXT_PLUGIN_ERROR);
                else {if(r==3)plugin_rules(&d,plugin_health,16,tr(TXT_HR_TITLE));
                    else if(r==1)plugin_rules(&d,fields[o],rules[o],names[o]);
                    else if(plugin_global_valid(&d,fields[o],counts[o]))plugin_notice(TXT_PLUGIN_ERROR);
                    else if(r==2)plugin_filters(&d);
                    else plugin_fields(&d,-1,fields[o],counts[o],names[o]);
                    free(d.text);}}
            plugin_keys_reset(&k);dirty=1;
        }
    }
}
