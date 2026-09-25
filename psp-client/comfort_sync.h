/* Explicit idle-menu sync only: never compete with audio/video networking.
 * No local downloads, connection profiles or passwords enter this payload. */
static void comfort_hex(char *out,const char *in) {
    static const char digits[]="0123456789abcdef";
    while(*in){unsigned char c=*in++;*out++=digits[c>>4];*out++=digits[c&15];}*out=0;
}
static int comfort_unhex(char *out,int capacity,const char *in) {
    int n=(int)strlen(in);if((n&1)||n/2>=capacity)return 0;
    for(int i=0;i<n;i+=2){int a=input_hex(in[i]),b=input_hex(in[i+1]);if(a<0||b<0||a*16+b<32)return 0;out[i/2]=(char)(a*16+b);}
    out[n/2]=0;return 1;
}
static int comfort_sync(void) {
    if(!network_ready || !http_ready || audio_running || timed_running)return 0;
    char client[17]={0},scope[80];comfort_scope(scope,0);
    const char *id_path="ms0:/PSP/SYSTEM/PSPStreamer-comfort.id";
    FILE *f=fopen(id_path,"r");if(f){fscanf(f,"%16s",client);fclose(f);}
    if(strlen(client)!=16) {
        snprintf(client,sizeof(client),"%08x%08x",(unsigned)comfort_checksum(&comfort_store),(unsigned)sceKernelGetSystemTimeWide());
        f=fopen(id_path,"w");if(!f)return 0;
        int ok=fputs(client,f)>=0;if(fclose(f)||!ok)return 0;
    }
    char *request=malloc(96000),*reply=malloc(100000);
    ComfortStore *merged=malloc(sizeof(*merged));
    if(!request||!reply||!merged){free(request);free(reply);free(merged);return 0;}
    int used=snprintf(request,96000,"{\"client\":\"%s\",\"records\":[",client),count=0;
    for(int i=0;i<COMFORT_RECORDS;i++) {
        ComfortRecord *r=&comfort_store.records[i];if(!r->id[0]||strcmp(scope,r->scope))continue;
        char id[1025],name[257];comfort_hex(id,r->id);comfort_hex(name,r->name);
        used+=snprintf(request+used,96000-used,"%s[\"%s\",\"%s\",%d,%d,%d,%d,%u]",count++?",":"",id,name,r->folder,r->audio,!!r->favorite,r->seconds,(unsigned)r->used);
    }
    snprintf(request+used,96000-used,"]}");
    volatile int running=1;
    settings_shell(tr(TXT_COMFORT));settings_line(2,0,tr(TXT_PLEASE_WAIT));settings_help(tr(TXT_COMFORT_HELP));
    int result=remote_http_request_budget("/api/comfort/sync",reply,100000,&running,10000,request),ok=result>=0;
    *merged=comfort_store;
    char *p=ok?strstr(reply,"[["):NULL;
    if(p)p++;
    if(ok && !p && !strstr(reply,"[]"))ok=0;
    int rows=0;
    while(ok && p && *p=='[') {
        char id[1025],name[257];int folder,audio,favorite,seconds,n=0;unsigned seq;
        if(++rows>64 || sscanf(p,"[\"%1024[0-9a-f]\", \"%256[0-9a-f]\", %d, %d, %d, %d, %u]%n",id,name,&folder,&audio,&favorite,&seconds,&seq,&n)!=7 || n<=0 ||
           folder<0||folder>1||audio<0||audio>1||favorite<0||favorite>1||seconds<0||seconds>86400){ok=0;break;}
        char key[512],label[128];if(!comfort_unhex(key,sizeof(key),id)||!comfort_unhex(label,sizeof(label),name)){ok=0;break;}
        int index=comfort_find(merged,scope,key,1);
        if(index>=0){ComfortRecord *r=&merged->records[index];strcpy(r->name,label);r->folder=folder;r->audio=audio;r->favorite=favorite;r->seconds=seconds;r->used=seq;}
        if(seq>merged->sequence)merged->sequence=seq;
        p+=n;while(*p==' '||*p==',')p++;
    }
    if(ok && comfort_save_file(COMFORT_PATH,merged))comfort_store=*merged;
    else ok=0;
    free(request);free(reply);free(merged);return ok;
}
