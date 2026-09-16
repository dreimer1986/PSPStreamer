/* Downloads run only in this modal screen, never beside playback. All socket,
 * TLS, file and hash ownership stays on the worker; the UI only cancels it. */
#include <pspiofilemgr_devctl.h>
#include <mbedtls/sha256.h>
#define OFFLINE_ROOT "ms0:/PSP/VIDEO/PSPStreamer"
#define OFFLINE_JOBS 128
typedef struct {char id[33],name[128],state[20],info[96];int progress;} OfflineEntry;
static OfflineEntry offline_entries[OFFLINE_JOBS];
static int offline_count;
static int download_catalog_request;
static volatile int download_running, download_done, download_result;
static volatile unsigned int download_bytes, download_total, download_speed;
static volatile int download_percent, download_stage;
static char download_key[33], download_post[1024], download_reply[8192];
static char download_error[160];

static int offline_key_valid(const char *s) {
    if(strlen(s)!=32)return 0;
    for(int i=0;i<32;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
static int offline_leaf_valid(const char *s) {
    if(!*s || strlen(s)>127 || !strcmp(s,".") || !strcmp(s,".."))return 0;
    for(;*s;s++)if((unsigned char)*s<32 || strchr("/\\:<>\"|?*",*s))return 0;
    return 1;
}
static char *offline_json_object_end(char *text) {
    int depth=0,quoted=0,escaped=0;
    for(;*text;text++) {
        if(quoted) {
            if(escaped)escaped=0;
            else if(*text=='\\')escaped=1;
            else if(*text=='"')quoted=0;
        } else if(*text=='"')quoted=1;
        else if(*text=='{')depth++;
        else if(*text=='}' && --depth==0)return text;
    }
    return NULL;
}
static int offline_text_load(const char *path,char *out,int capacity) {
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return -1;
    int n=sceIoRead(fd,out,capacity-1);sceIoClose(fd);
    if(n<0)return n;
    out[n]=0;return n;
}
static int offline_write(const char *path,const char *data,int size) {
    SceUID fd=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    if(fd<0)return fd;
    int n=sceIoWrite(fd,data,size),closed=sceIoClose(fd);return n==size && closed>=0?0:-1;
}
static unsigned long long offline_size(const char *path) {
    SceIoStat st;if(sceIoGetstat(path,&st)<0)return 0;return st.st_size;
}
static int offline_connect(int fd) {
    struct sockaddr_in address;int nonblock=1,error=0;socklen_t size=sizeof(error);
    if(prepare_server(&address)<0)return -1;
    if(sceNetInetSetsockopt(fd,SOL_SOCKET,SO_NONBLOCK,&nonblock,sizeof(nonblock))<0)return -1;
    unsigned long long until=sceKernelGetSystemTimeWide()+15000000ULL;
    if(sceNetInetConnect(fd,(struct sockaddr *)&address,sizeof(address))<0) {
        while(download_running && (unsigned long long)sceKernelGetSystemTimeWide()<until) {
            struct SceNetInetPollfd p={fd,SCE_NET_INET_POLLOUT,0};
            int n=sceNetInetPoll(&p,1,100);if(n<0)return -1;if(!n)continue;
            if(!(p.revents&SCE_NET_INET_POLLOUT) || sceNetInetGetsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error)return -1;
            goto connected;
        }
        return -1;
    }
connected:
    if(!download_running)return -1;
    return server_https?tls_open(fd,server_host,server_port,&download_running,15000):0;
}
/* Strict finite HTTP body. Resume must be acknowledged with the exact range.
 * Never append a 200/error page to a partial FLV. */
static int offline_http(const char *url,const char *post,char *reply,int capacity,
                        const char *file,unsigned int expected) {
    char header[4096],request[4096],range[80]="";unsigned char *block=NULL;
    int fd=-1,out=-1,n=0,result=-1,code=0;unsigned int have=0,start=0;
    unsigned long long length=0,last,tick=sceKernelGetSystemTimeWide();
    if(file){unsigned long long old=offline_size(file);if(old>expected)return -1;start=(unsigned int)old;}
    if(start)snprintf(range,sizeof(range),"Range: bytes=%u-\r\n",start);
    int wanted=snprintf(request,sizeof(request),"%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s%s%sContent-Length: %u\r\n\r\n%s",
        post?"POST":"GET",url,server_host,server_auth_header,range,post?"Content-Type: application/json\r\n":"",
        post?(unsigned int)strlen(post):0,post?post:"");
    if(wanted<0||wanted>=(int)sizeof(request))goto done;
    fd=sceNetInetSocket(AF_INET,SOCK_STREAM,0);if(fd<0||offline_connect(fd)<0)goto done;
    for(int sent=0;sent<wanted && download_running;) {
        int got=server_https?tls_send(fd,request+sent,wanted-sent,&download_running,15000):(int)sceNetInetSend(fd,request+sent,wanted-sent,0);
        if(got<=0)goto done;
        sent+=got;
    }
    last=sceKernelGetSystemTimeWide();
    while(download_running && n<(int)sizeof(header)-1) {
        int got=stream_recv(fd,header+n,1,100);
        if(got==-2){if(sceKernelGetSystemTimeWide()-last<30000000ULL)continue;goto done;}
        if(got<=0)goto done;
        last=sceKernelGetSystemTimeWide();header[++n]=0;
        if(n>=4&&!memcmp(header+n-4,"\r\n\r\n",4))break;
    }
    if(!download_running || !strstr(header,"\r\n\r\n") || sscanf(header,"HTTP/%*s %d",&code)!=1)goto done;
    char *value=strstr(header,"Content-Length:");if(!value)goto done;
    length=strtoull(value+15,NULL,10);
    if(code!=(start?206:200)) {snprintf(download_error,sizeof(download_error),"HTTP %d",code);goto done;}
    if(start) {
        unsigned int first;unsigned long long end,total;
        value=strstr(header,"Content-Range:");
        if(!value || sscanf(value,"Content-Range: bytes %u-%llu/%llu",&first,&end,&total)!=3 || first!=start || total!=expected || end+1!=total)goto done;
    }
    if(file) {
        if(length!=(unsigned long long)expected-start)goto done;
        out=sceIoOpen(file,PSP_O_WRONLY|PSP_O_CREAT,0777);if(out<0)goto done;
        if(sceIoLseek(out,start,PSP_SEEK_SET)<0)goto done;
        download_bytes=start;download_total=expected;
    } else if(length>=(unsigned int)capacity)goto done;
    block=malloc(32768);if(!block)goto done;
    while(download_running && have<length) {
        unsigned int count=length-have;if(count>32768)count=32768;
        int got=stream_recv(fd,block,count,100);
        if(got==-2){if(sceKernelGetSystemTimeWide()-last<30000000ULL)continue;goto done;}
        if(got<=0)goto done;
        last=sceKernelGetSystemTimeWide();
        if(file){if(sceIoWrite(out,block,got)!=got){snprintf(download_error,sizeof(download_error),"Memory Stick write failed");goto done;}}
        else memcpy(reply+have,block,got);
        have+=got;
        if(file) {
            download_bytes=start+have;
            unsigned long long elapsed=last-tick;
            download_speed=elapsed?((unsigned long long)have*1000000ULL/elapsed):0;
        }
    }
    if(have!=length)goto done;
    if(!file)reply[have]=0;
    result=0;
done:
    free(block);
    if(out>=0 && sceIoClose(out)<0)result=-1;
    if(fd>=0)connection_close(fd);
    return result;
}
static int offline_hash(const char *path,const char *expected) {
    unsigned char block[16384],digest[32];char hex[65];
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return -1;
    mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);mbedtls_sha256_starts_ret(&ctx,0);
    int n=0;
    while(download_running && (n=sceIoRead(fd,block,sizeof(block)))>0)mbedtls_sha256_update_ret(&ctx,block,n);
    mbedtls_sha256_finish_ret(&ctx,digest);mbedtls_sha256_free(&ctx);sceIoClose(fd);
    if(n<0||!download_running)return -1;
    for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    return strcmp(hex,expected)?-1:0;
}
static int offline_download_worker(SceSize args,void *argp) {
    char url[128],folder[384],path[512],meta[8192],state[24];int result=-1;
    (void)args;(void)argp;
    /* Re-associate only this offline worker; leave streaming timeouts alone. */
    download_stage=3;
    int connected=0;
    for(int i=0;i<200 && download_running;i++) {
        int ap=0;
        if(sceNetApctlGetState(&ap)<0)break;
        if(ap==PSP_NET_APCTL_STATE_GOT_IP){connected=1;break;}
        if(active_network_profile<=0)break;
        if(ap==0 && i%20==0)sceNetApctlConnect(active_network_profile);
        sceKernelDelayThread(100000);
    }
    if(!connected){snprintf(download_error,sizeof(download_error),"%s",tr(TXT_WIFI_NOT_READY));goto done;}
    if(download_catalog_request) {
        result=offline_http("/api/offline/catalog",NULL,response,sizeof(response),NULL,0);
        goto done;
    }
    if(download_post[0]) {
        download_stage=0;
        if(offline_http("/api/offline/jobs",download_post,download_reply,sizeof(download_reply),NULL,0)<0 ||
           !json_value(download_reply,"job",download_key,sizeof(download_key)))goto done;
    }
    if(!offline_key_valid(download_key))goto done;
    snprintf(url,sizeof(url),"/api/offline/job/%s",download_key);
    while(download_running) {
        if(offline_http(url,NULL,meta,sizeof(meta),NULL,0)<0)goto done;
        if(!json_value(meta,"state",state,sizeof(state)))goto done;
        download_percent=json_integer(meta,"progress",0);download_stage=0;
        if(!strcmp(state,"ready"))break;
        if(strcmp(state,"queued")&&strcmp(state,"encoding")) {
            json_value(meta,"error",download_error,sizeof(download_error));goto done;
        }
        for(int i=0;i<20&&download_running;i++)sceKernelDelayThread(100000);
    }
    if(!download_running)goto done;
    sceIoMkdir("ms0:/PSP/VIDEO",0777);sceIoMkdir(OFFLINE_ROOT,0777);
    snprintf(folder,sizeof(folder),"%s/%s",OFFLINE_ROOT,download_key);sceIoMkdir(folder,0777);
    snprintf(path,sizeof(path),"%s/job.json",folder);if(offline_write(path,meta,strlen(meta))<0)goto done;
    snprintf(path,sizeof(path),"%s/ready",folder);sceIoRemove(path);
    char *cursor=strstr(meta,"\"files\":[");if(!cursor)goto done;
    cursor+=9;
    for(int i=0;i<3;i++) {
        char leaf[128],hash[65],part[520];
        char *end=offline_json_object_end(cursor);if(!end)goto done;
        char save=end[1];end[1]=0;
        int ok=json_value(cursor,"name",leaf,sizeof(leaf))&&json_value(cursor,"sha256",hash,sizeof(hash));
        char *sz=strstr(cursor,"\"size\":");unsigned long long size=sz?strtoull(sz+7,NULL,10):0;
        end[1]=save;
        if(!ok||!offline_leaf_valid(leaf)||size>0xFFFFFFFFULL)goto done;
        snprintf(path,sizeof(path),"%s/%s",folder,leaf);snprintf(part,sizeof(part),"%s.part",path);
        download_stage=2;
        if(offline_size(path)==size && offline_hash(path,hash)==0){cursor=end+1;continue;}
        unsigned long long old=offline_size(part);
        if(old>size){sceIoRemove(part);old=0;}
        SceDevInf capacity;SceDevctlCmd command={&capacity};
        memset(&capacity,0,sizeof(capacity));
        if(sceIoDevctl("ms0:",SCE_PR_GETDEV,&command,sizeof(command),NULL,0)<0 ||
            (unsigned long long)capacity.freeClusters*capacity.sectorSize*capacity.sectorCount<size-old+1048576ULL) {
            snprintf(download_error,sizeof(download_error),"%s",tr(TXT_DOWNLOAD_SPACE));goto done;
        }
        download_stage=1;download_bytes=old;download_total=size;download_speed=0;
        snprintf(url,sizeof(url),"/api/offline/file/%s/%d",download_key,i);
        if(old<size && offline_http(url,NULL,NULL,0,part,size)<0)goto done;
        if(!size && offline_write(part,"",0)<0)goto done;
        download_stage=2;
        if(offline_hash(part,hash)<0) {
            if(download_running){sceIoRemove(part);snprintf(download_error,sizeof(download_error),"SHA256 mismatch; retry download");}
            goto done;
        }
        sceIoRemove(path);
        if(sceIoRename(part,path)<0)goto done;
        cursor=end+1;
    }
    snprintf(path,sizeof(path),"%s/ready",folder);
    result=offline_write(path,"1",1);
done:
    download_result=result;download_done=1;return 0;
}
static int offline_transfer(const char *key,const char *post) {
    snprintf(download_key,sizeof(download_key),"%s",key?key:"");
    snprintf(download_post,sizeof(download_post),"%s",post?post:"");
    download_done=download_result=download_stage=download_percent=0;download_running=1;
    download_bytes=download_total=download_speed=0;download_error[0]=0;
    SceUID worker=sceKernelCreateThread("OfflineDownload",offline_download_worker,0x30,0x20000,0,NULL);
    if(worker<0)return worker;
    int started=sceKernelStartThread(worker,0,NULL);
    if(started<0){sceKernelDeleteThread(worker);return started;}
    unsigned int old=PSP_CTRL_CROSS;
    while(!download_done) {
        SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons&PSP_CTRL_CIRCLE)&&!(old&PSP_CTRL_CIRCLE))download_running=0;
        settings_shell(tr(TXT_DOWNLOADS));char line[100];
        settings_line(0,0,tr(!download_running?TXT_DOWNLOAD_STOPPING:download_stage==3?TXT_CONNECTING_WIFI:download_stage==0?TXT_CONVERTING:download_stage==1?TXT_DOWNLOADING:TXT_VERIFYING));
        unsigned int percent=download_stage==1 && download_total ? (unsigned int)((unsigned long long)download_bytes*100/download_total) : (unsigned int)download_percent;
        if(percent>100)percent=100;
        snprintf(line,sizeof(line),"%u%%",percent);settings_line(2,0,line);
        if(download_stage==1) {
            unsigned int done=download_bytes,total=download_total,speed=download_speed;
            if(done>total)done=total;
            snprintf(line,sizeof(line),"%.1f / %.1f MiB",done/1048576.0,total/1048576.0);settings_line(3,0,line);
            snprintf(line,sizeof(line),"%.1f KiB/s",speed/1024.0);settings_line(4,0,line);
            if(speed)snprintf(line,sizeof(line),tr(TXT_DOWNLOAD_ETA),(total-done)/speed);
            else snprintf(line,sizeof(line),"%s",tr(TXT_PLEASE_WAIT));
            settings_line(5,0,line);
        }
        settings_help(tr(TXT_DOWNLOAD_CANCEL));old=pad.Buttons;sceKernelDelayThread(100000);
    }
    sceKernelWaitThreadEnd(worker,NULL);sceKernelDeleteThread(worker);
    download_running=0;
    if(download_result<0) {
        snprintf(status,sizeof(status),"%s: %.100s",tr(TXT_DOWNLOAD_RETRY),download_error);
        settings_shell(tr(TXT_DOWNLOADS));settings_line(1,0,tr(TXT_DOWNLOAD_RETRY));settings_line(3,0,download_error);
        settings_help(tr(TXT_DOWNLOAD_BACK));
        SceCtrlData pad;unsigned int previous=PSP_CTRL_CIRCLE|PSP_CTRL_CROSS;
        while(1){keep_awake();sceCtrlReadBufferPositive(&pad,1);if((pad.Buttons&~previous)&(PSP_CTRL_CIRCLE|PSP_CTRL_CROSS))break;previous=pad.Buttons;sceKernelDelayThread(20000);}
    }
    return download_result;
}

static void offline_scan(void) {
    SceIoDirent entry;char path[512],meta[8192];offline_count=0;
    SceUID dir=sceIoDopen(OFFLINE_ROOT);if(dir<0)return;
    memset(&entry,0,sizeof(entry));
    while(offline_count<OFFLINE_JOBS && sceIoDread(dir,&entry)>0) {
        if(offline_key_valid(entry.d_name)) {
            snprintf(path,sizeof(path),"%s/%s/job.json",OFFLINE_ROOT,entry.d_name);
            if(offline_text_load(path,meta,sizeof(meta))>0) {
                OfflineEntry *item=&offline_entries[offline_count];
                if(json_value(meta,"name",item->name,sizeof(item->name)) && offline_leaf_valid(item->name)) {
                    snprintf(item->id,sizeof(item->id),"%s",entry.d_name);
                    snprintf(path,sizeof(path),"%s/%s/ready",OFFLINE_ROOT,entry.d_name);
                    snprintf(item->state,sizeof(item->state),"%s",offline_size(path)?"ready":"partial");
                    char audio[20]="",sub[20]="",profile[16]="";
                    json_value(meta,"audio_label",audio,sizeof(audio));json_value(meta,"subtitle_label",sub,sizeof(sub));json_value(meta,"profile",profile,sizeof(profile));
                    snprintf(item->info,sizeof(item->info),"%s | A%d %s | S%d %s",!strcmp(profile,"tv")?"TV 720x480":"LCD 480x272",json_integer(meta,"audio",0)+1,audio,json_integer(meta,"subtitle",-1)+1,sub);
                    item->progress=!strcmp(item->state,"ready")?100:0;offline_count++;
                }
            }
        }
        memset(&entry,0,sizeof(entry));
    }
    sceIoDclose(dir);
    /* Deterministic filename ordering, independent of FAT insertion. */
    for(int i=1;i<offline_count;i++)for(int j=i;j>0 && strcmp(offline_entries[j-1].name,offline_entries[j].name)>0;j--) {
        OfflineEntry swap=offline_entries[j];offline_entries[j]=offline_entries[j-1];offline_entries[j-1]=swap;
    }
}
static void offline_catalog(void) {
    settings_shell(tr(TXT_DOWNLOADS));settings_line(1,0,tr(TXT_PLEASE_WAIT));settings_help(tr(TXT_DOWNLOAD_BACK));
    offline_count=0;
    download_catalog_request=1;
    int result=offline_transfer(NULL,NULL);
    download_catalog_request=0;
    if(result<0)return;
    char *line=response;
    while(line && *line && offline_count<OFFLINE_JOBS) {
        char *next=strchr(line,'\n');if(next)*next++=0;
        OfflineEntry *item=&offline_entries[offline_count];
        item->info[0]=0;
        if(sscanf(line,"%32[^\t]\t%19[^\t]\t%d\t%127[^\n]",item->id,item->state,&item->progress,item->name)==4 && offline_key_valid(item->id))offline_count++;
        line=next;
    }
}
static int offline_confirm(const char *name) {
    unsigned int old=PSP_CTRL_TRIANGLE;SceCtrlData pad;
    settings_shell(tr(TXT_LOCAL_STORAGE));settings_line(1,0,name);
    settings_line(3,0,tr(TXT_DOWNLOAD_DELETE));settings_help(tr(TXT_DOWNLOAD_CONFIRM));
    while(1){keep_awake();sceCtrlReadBufferPositive(&pad,1);unsigned int pressed=pad.Buttons&~old;
        if(pressed&PSP_CTRL_CIRCLE)return 0;
        if(pressed&PSP_CTRL_CROSS)return 1;
        old=pad.Buttons;sceKernelDelayThread(20000);}
}
static void offline_delete(const OfflineEntry *item) {
    char path[512];const char *leaves[]={item->name,"subtitles.ovl","seek.idx","job.json","ready"};
    if(!offline_key_valid(item->id)||!offline_leaf_valid(item->name))return;
    for(int i=0;i<5;i++) {
        snprintf(path,sizeof(path),"%s/%s/%s",OFFLINE_ROOT,item->id,leaves[i]);sceIoRemove(path);
        if(i<3){snprintf(path,sizeof(path),"%s/%s/%s.part",OFFLINE_ROOT,item->id,leaves[i]);sceIoRemove(path);}
    }
    snprintf(path,sizeof(path),"%s/%s",OFFLINE_ROOT,item->id);sceIoRmdir(path);
}
static int offline_play(const OfflineEntry *item) {
    char meta[8192],path[512],profile[16];
    snprintf(offline_directory,sizeof(offline_directory),"%s/%s",OFFLINE_ROOT,item->id);
    snprintf(path,sizeof(path),"%s/job.json",offline_directory);
    if(offline_text_load(path,meta,sizeof(meta))<0)return -1;
    snprintf(offline_movie,sizeof(offline_movie),"%s/%s",offline_directory,item->name);
    char *duration=strstr(meta,"\"duration\":");current_duration_seconds=duration?atof(duration+11):0;
    offline_profile_tv=json_value(meta,"profile",profile,sizeof(profile))&&!strcmp(profile,"tv");
    offline_active=1;stream_start_seconds=0;resume_pending=seek_requested=0;
    int result;
    do {
        seek_requested=0;
        result=play_h264(item->id);
        ui_restore_after_playback();
        if(result<0 || !seek_requested)break;
        sceKernelDelayThread(250000);
    } while(1);
    offline_active=0;offline_movie[0]=offline_directory[0]=0;resume_pending=seek_requested=0;
    if(result<0) {
        snprintf(status,sizeof(status),"%s: %08X",video_step,result);
        settings_shell(tr(TXT_LOCAL_STORAGE));settings_line(1,0,video_step);
        char code[20];snprintf(code,sizeof(code),"%08X",result);settings_line(3,0,code);
        if(result==-1401) {
            settings_line(5,0,tr(offline_profile_tv?TXT_DOWNLOAD_TV_FILE:TXT_DOWNLOAD_LCD_FILE));
            settings_line(7,0,tr(offline_profile_tv?TXT_DOWNLOAD_TV_HELP:TXT_DOWNLOAD_LCD_HELP));
        }
        settings_help(tr(TXT_DOWNLOAD_BACK));
        SceCtrlData pad;unsigned int old=PSP_CTRL_CROSS|PSP_CTRL_START;
        while(1){keep_awake();sceCtrlReadBufferPositive(&pad,1);if((pad.Buttons&~old)&(PSP_CTRL_CROSS|PSP_CTRL_CIRCLE))break;old=pad.Buttons;sceKernelDelayThread(20000);}
    }
    return result;
}
static void offline_browser(void) {
    int server=0,selected=0,dirty=1;unsigned int old=PSP_CTRL_CIRCLE|PSP_CTRL_CROSS;
    unsigned long long repeat=0;offline_scan();
    while(1) {
        SceCtrlData pad;keep_awake();sceCtrlReadBufferPositive(&pad,1);
        unsigned int pressed=pad.Buttons&~old;
        if(dirty) {
            settings_shell(tr(server?TXT_DOWNLOADS:TXT_LOCAL_STORAGE));
            if(!offline_count)settings_line(0,0,tr(TXT_NO_ENTRIES));
            int first=selected/7*7;
            for(int i=first;i<offline_count&&i<first+7;i++) {
                char line[192];snprintf(line,sizeof(line),"%c %.37s",!strcmp(offline_entries[i].state,"ready")?'+':'~',offline_entries[i].name);
                settings_line(i-first,i==selected,line);
            }
            if(offline_count){char line[100];snprintf(line,sizeof(line),"%.19s %d%% | %.32s",offline_entries[selected].state,offline_entries[selected].progress,offline_entries[selected].id);settings_line(8,0,line);}
            if(offline_count)settings_line(9,0,offline_entries[selected].info);
            settings_help(tr(server?TXT_DOWNLOAD_QUEUE_HELP:TXT_DOWNLOAD_LOCAL_HELP));dirty=0;
        }
        if(pressed&PSP_CTRL_CIRCLE)return;
        if(pressed&PSP_CTRL_SQUARE){server=!server;selected=0;if(server)offline_catalog();else offline_scan();dirty=1;}
        if(offline_count && (pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN))) {
            unsigned long long now=sceKernelGetSystemTimeWide();
            if((pressed&(PSP_CTRL_UP|PSP_CTRL_DOWN))||now>=repeat) {
                selected=(selected+((pad.Buttons&PSP_CTRL_DOWN)?1:offline_count-1))%offline_count;repeat=now+150000;dirty=1;
            }
        }
        if(offline_count && !server && (pressed&PSP_CTRL_TRIANGLE)) {
            if(offline_confirm(offline_entries[selected].name))offline_delete(&offline_entries[selected]);
            offline_scan();if(selected>=offline_count)selected=0;dirty=1;
            sceCtrlReadBufferPositive(&pad,1);old=pad.Buttons;continue;
        }
        if(offline_count && server && (pressed&PSP_CTRL_RTRIGGER)) {
            for(int i=0;i<offline_count;i++) {
                if(strcmp(offline_entries[i].state,"ready")&&strcmp(offline_entries[i].state,"queued")&&strcmp(offline_entries[i].state,"encoding"))continue;
                if(offline_transfer(offline_entries[i].id,NULL)<0)break;
            }
            server=0;selected=0;offline_scan();dirty=1;
            sceCtrlReadBufferPositive(&pad,1);old=pad.Buttons;continue;
        }
        if(offline_count && (pressed&PSP_CTRL_CROSS)) {
            if(server || strcmp(offline_entries[selected].state,"ready")) {
                offline_transfer(offline_entries[selected].id,NULL);
                server=0;selected=0;offline_scan();
            } else {
                int result;
                do {
                    result=offline_play(&offline_entries[selected]);
                    if(result<0 || (!playback_reached_end&&!video_file_direction))break;
                    int next=selected+(video_file_direction<0?-1:1);
                    if(next<0||next>=offline_count||strcmp(offline_entries[next].state,"ready"))break;
                    selected=next;sceKernelDelayThread(250000);
                } while(1);
            }
            dirty=1;
            sceCtrlReadBufferPositive(&pad,1);
        }
        old=pad.Buttons;sceKernelDelayThread(30000);
    }
}
static void offline_enqueue_play(const char *media_id) {
    char post[1024];
    snprintf(post,sizeof(post),"{\"id\":\"%s\",\"audio\":%d,\"subtitle\":%d,\"audio_quality\":\"%s\",\"video_fps\":\"%s\",\"profile\":\"%s\"}",
        media_id,selected_audio_track,selected_subtitle_track,audio_quality_name(),selected_video_fps?"24000/1001":"20",
        (tvout_load_manager()==0 && pspDveMgrCheckVideoOut()==2)?"tv":PSP_STREAMER_PROFILE);
    if(offline_transfer(NULL,post)==0) {
        offline_scan();for(int i=0;i<offline_count;i++)if(!strcmp(offline_entries[i].id,download_key)){offline_play(&offline_entries[i]);break;}
    }
}
