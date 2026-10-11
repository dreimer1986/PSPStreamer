/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional VSH-only FTP service. Files stay on PSP; ESP supplies passive TCP.
 * No hooks, no USB ownership/reset, no streaming socket changes. */
#ifdef PSP_FTP_HOST_TEST
#include "../tests/ftp_host_platform.h"
#else
#include <pspkernel.h>
#include <pspinit.h>
#include <pspsysmem_kernel.h>
#include <pspiofilemgr.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../streammaster/protocol.h"
#include "../streammaster/tcp_server.h"
#include "path.h"
#include "size_text.h"
PSP_MODULE_INFO("PSPStreamFTP",PSP_MODULE_KERNEL,0,3);
#define BASE "ms0:/SEPLUGINS/PSPStreamFTP/"
#define CONTROL 0x46545002U
#define LISTENER 0x46545001U
#define PASSIVE 0x46545003U
#define DATA 0x46545004U
#define REFUSED 0x46545005U
static volatile int running;
static SceUID thread=-1,memory=-1;
static int enabled,port=2121,idle_seconds=300,read_only;
static char username[64],password[128];
typedef struct {
    SmFrame request,reply;
    char cwd[FTP_PATH],control[1024],data[4000],rename[FTP_PATH];
    /* Owned by the single FTP worker. Keep large temporaries out of scarce
     * kernel thread stacks, including nested reply/transfer call chains. */
    char config_text[2048],command_text[512],response_text[1200];
    unsigned char write_payload[4008];
    unsigned used,peer,serial,sequence;
    int authenticated,user_ok,quit,passive,aborted,failures;
    SceOff restart;
    char ip[16];
} Session;
static Session *s;
static unsigned char upper(unsigned char c){return c>='a' && c<='z'?c-32:c;}
static int ascii_equal(const char *a,const char *b){while(*a && upper(*a)==upper(*b)){a++;b++;}return upper(*a)==upper(*b);}
static int decimal(const char *p,unsigned limit,unsigned *out){
    unsigned n=0;if(!*p)return -1;
    for(;*p;p++){if(*p<'0' || *p>'9' || n>limit/10 || (n==limit/10 && (unsigned)(*p-'0')>limit%10))return -1;n=n*10+(*p-'0');}
    *out=n;return 0;
}
static unsigned long long clock_us(void){return sceKernelGetSystemTimeWide();}
static void status(const char *message,int code){
    char line[192];int n=snprintf(line,sizeof(line),"%s: %08X\n",message,(unsigned)code);
    int f=sceIoOpen(BASE "status.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0600);
    if(f>=0){if(n>0)sceIoWrite(f,line,n<(int)sizeof(line)?n:(int)sizeof(line)-1);sceIoClose(f);}
}
static int config(void){
    char *buffer=s->config_text;int f=sceIoOpen(BASE "PSPStreamFTP.ini",PSP_O_RDONLY,0);
    if(f<0)return -1;
    int n=sceIoRead(f,buffer,sizeof(s->config_text)-1);sceIoClose(f);
    if(n<=0 || n==(int)sizeof(s->config_text)-1)return -1;
    buffer[n]=0;
    char *save=NULL;
    for(char *p=strtok_r(buffer,"\r\n",&save);p;p=strtok_r(NULL,"\r\n",&save)){
        while(*p==' ' || *p=='\t')p++;
        if(!strncmp(p,"enabled=",8))enabled=!strcmp(p+8,"1");
        else if(!strncmp(p,"user=",5)){if(strlen(p+5)>=sizeof(username))return -1;strcpy(username,p+5);}
        else if(!strncmp(p,"password=",9)){if(strlen(p+9)>=sizeof(password))return -1;strcpy(password,p+9);}
        else if(!strncmp(p,"port=",5)){unsigned v;if(decimal(p+5,65535,&v)<0)return -1;port=v;}
        else if(!strncmp(p,"idle_seconds=",13)){unsigned v;if(decimal(p+13,3600,&v)<0)return -1;idle_seconds=v;}
        else if(!strncmp(p,"read_only=",10))read_only=!strcmp(p+10,"1");
    }
    memset(buffer,0,sizeof(s->config_text));
    return port>0 && port<=65535 && idle_seconds>=30 && idle_seconds<=3600 && username[0] && password[0]?0:-1;
}
static int rpc(unsigned op,const void *data,unsigned n){
    if(!running)return SM_OFFLINE;
    SmFrame *r=&s->request;memset(r,0,sizeof(*r));
    r->op=op;r->sequence=++s->sequence;r->length=n;
    if(n)memcpy(r->payload,data,n);
    sm_seal(r);
    int rc=sceIoDevctl("stm:",SM_DEV_EXCHANGE_COMPACT,r,sizeof(*r),&s->reply,sizeof(s->reply));
    if(rc<0)return rc;
    if(!sm_valid(&s->reply) || s->reply.op!=op || s->reply.sequence!=r->sequence || s->reply.flags!=SM_REPLY)return SM_IO;
    return s->reply.result;
}
static void close_socket(unsigned token){SmSocketRequest p={token,0};rpc(SM_TCP_CLOSE,&p,sizeof(p));}
static int read_socket(unsigned token,void *out,unsigned cap){
    SmSocketRequest p={token,cap};int rc=rpc(SM_TCP_READ,&p,sizeof(p));
    if(rc<0)return rc;
    if(s->reply.length>cap)return SM_IO;
    memcpy(out,s->reply.payload,s->reply.length);return s->reply.length;
}
static int send_socket(unsigned token,const void *data,unsigned n){
    unsigned char *payload=s->write_payload;SmSocketRequest p={token,n};
    if(n>4000)return SM_INVALID;
    memcpy(payload,&p,sizeof(p));memcpy(payload+sizeof(p),data,n);
    return rpc(SM_TCP_WRITE,payload,sizeof(p)+n);
}
static int send_all(unsigned token,const char *p,unsigned n){
    unsigned long long until=clock_us()+5000000;
    while(n && running){
        unsigned amount=n>4000?4000:n;int rc=send_socket(token,p,amount);
        if(rc>0 && (unsigned)rc<=amount){p+=rc;n-=rc;until=clock_us()+5000000;}
        else if(rc!=SM_BUSY || clock_us()>=until)return -1;
        else sceKernelDelayThread(10000);
    }
    return n?-1:0;
}
static void refuse_extra_connection(void){
    /* FileZilla normally uses separate browsing and transfer sessions. Do not
     * leave the second socket in listen backlog until its client times out. */
    SmTcpAccept p={LISTENER,REFUSED};
    if(rpc(SM_TCP_ACCEPT,&p,sizeof(p))==0){
        static const char message[]="421 One session only; set Site Manager maximum connections to 1.\r\n";
        send_socket(REFUSED,message,sizeof(message)-1);close_socket(REFUSED);
    }
}
#define reply(...) do { int ftp_n=snprintf(s->response_text,sizeof(s->response_text),__VA_ARGS__); \
    if(ftp_n<=0 || ftp_n>=(int)sizeof(s->response_text) || send_all(CONTROL,s->response_text,ftp_n)<0)s->quit=1; } while(0)
static int command_line(char *line,unsigned cap){
    for(;;){
        char *end=memchr(s->control,'\n',s->used);
        if(end){
            unsigned consumed=end-s->control+1,n=consumed-1;
            if(n && s->control[n-1]=='\r')n--;
            if(n>=cap || memchr(s->control,0,n))return -1;
            memcpy(line,s->control,n);line[n]=0;
            memmove(s->control,s->control+consumed,s->used-consumed);s->used-=consumed;return 1;
        }
        if(s->used==sizeof(s->control))return -1;
        int n=read_socket(CONTROL,s->control+s->used,sizeof(s->control)-s->used);
        if(n==SM_BUSY)return 0;
        if(n<=0)return -1;
        s->used+=n;
    }
}
static int interrupted(void){
    if(!running || s->quit)return 1;
    char line[512];int n=command_line(line,sizeof(line));
    if(n<0){s->quit=1;return 1;}
    if(n>0){
        if(ascii_equal(line,"ABOR")){s->aborted=1;return 1;}
        if(ascii_equal(line,"QUIT")){s->quit=1;return 1;}
        reply("450 Transfer in progress; use ABOR.\r\n");
    }
    return 0;
}
static int path_for(const char *arg,char virtual_path[FTP_PATH],char real[FTP_PATH+4]){
    if(ftp_path(s->cwd,arg,virtual_path)<0)return -1;
    snprintf(real,FTP_PATH+4,"ms0:%s",virtual_path);return 0;
}
static int accept_data(void){
    if(!s->passive){reply("425 Use PASV or EPSV first.\r\n");return -1;}
    reply("150 Opening data connection.\r\n");
    SmTcpAccept p={PASSIVE,DATA};unsigned long long until=clock_us()+15000000;
    while(running && !interrupted() && clock_us()<until){
        int rc=rpc(SM_TCP_ACCEPT,&p,sizeof(p));
        if(!rc){close_socket(PASSIVE);s->passive=0;return 0;}
        if(rc!=SM_BUSY)break;
        sceKernelDelayThread(10000);
    }
    close_socket(PASSIVE);s->passive=0;reply("425 Data connection unavailable.\r\n");return -1;
}
static int unique_path(const char *path,char out[FTP_PATH+48],const char *kind){
    SceIoStat st;
    for(unsigned i=0;i<16;i++){
        snprintf(out,FTP_PATH+48,"%s.smftp-%s-%08X",path,kind,++s->serial);
        if(sceIoGetstat(out,&st)==(int)0x80010002)return 0;
    }
    return -1;
}
static int finish_upload(const char *temp,const char *path){
    SceIoStat st;int exists=sceIoGetstat(path,&st);
    if(exists<0)return sceIoRename(temp,path);
    if(FIO_S_ISDIR(st.st_mode))return -1;
    char backup[FTP_PATH+48];if(unique_path(path,backup,"backup")<0 || sceIoRename(path,backup)<0)return -1;
    int rc=sceIoRename(temp,path);
    if(rc<0)sceIoRename(backup,path);else sceIoRemove(backup);
    return rc;
}
static void transfer(const char *verb,const char *arg){
    char virtual_path[FTP_PATH],path[FTP_PATH+4],temp[FTP_PATH+48]={0};
    int listing=!strcmp(verb,"LIST") || !strcmp(verb,"NLST") || !strcmp(verb,"MLSD"),upload=!strcmp(verb,"STOR");
    SceOff offset=s->restart;s->restart=0;s->aborted=0;
    if(upload && read_only){reply("550 Read-only service.\r\n");return;}
    if(upload && offset){reply("504 Upload resume is not supported.\r\n");return;}
    if(listing && (*arg=='-' || !*arg))arg=".";
    if(path_for(arg,virtual_path,path)<0 || (upload && !strcmp(virtual_path,"/"))){reply("550 Invalid path.\r\n");return;}
    int file;
    if(listing)file=sceIoDopen(path);
    else if(upload){
        if(unique_path(path,temp,"part")<0){reply("550 Cannot create upload staging file.\r\n");return;}
        file=sceIoOpen(temp,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_EXCL,0666);
    }else file=sceIoOpen(path,PSP_O_RDONLY,0);
    if(file<0){reply("550 Cannot open path.\r\n");return;}
    int good=1;
    if(offset && !listing && sceIoLseek(file,offset,PSP_SEEK_SET)!=offset)good=0;
    if(good && accept_data()<0)good=0;
    unsigned long long deadline=clock_us()+30000000;
    while(good && running){
        if(interrupted()){good=0;break;}
        int n=0;
        if(listing){
            SceIoDirent entry;memset(&entry,0,sizeof(entry));n=sceIoDread(file,&entry);
            if(n<=0){if(n<0)good=0;break;}
            if(!strcmp(entry.d_name,".") || !strcmp(entry.d_name,"..") || strchr(entry.d_name,'\r') || strchr(entry.d_name,'\n'))continue;
            int dir=FIO_S_ISDIR(entry.d_stat.st_mode);
            char size_text[21];ftp_size_text((uint64_t)entry.d_stat.st_size,size_text);
            if(!strcmp(verb,"NLST"))n=snprintf(s->data,sizeof(s->data),"%s\r\n",entry.d_name);
            else if(!strcmp(verb,"MLSD"))n=snprintf(s->data,sizeof(s->data),"type=%s;size=%s; %s\r\n",dir?"dir":"file",size_text,entry.d_name);
            else {
                static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
                unsigned month=entry.d_stat.sce_st_mtime.month;
                n=snprintf(s->data,sizeof(s->data),"%crw-r--r-- 1 psp psp %s %s %02u %04u %s\r\n",dir?'d':'-',size_text,
                    months[month>=1 && month<=12?month-1:0],(unsigned)entry.d_stat.sce_st_mtime.day,(unsigned)entry.d_stat.sce_st_mtime.year,entry.d_name);
            }
        }else if(upload){
            n=read_socket(DATA,s->data,sizeof(s->data));
            if(n==SM_BUSY){if(clock_us()>=deadline){good=0;break;}sceKernelDelayThread(10000);continue;}
            if(n<=0){if(n<0)good=0;break;}
            int done=0;while(done<n){int wrote=sceIoWrite(file,s->data+done,n-done);if(wrote<=0){good=0;break;}done+=wrote;}
            deadline=clock_us()+30000000;continue;
        }else{n=sceIoRead(file,s->data,sizeof(s->data));if(n<=0){if(n<0)good=0;break;}}
        if(n>=(int)sizeof(s->data) && listing){good=0;break;}
        if(send_all(DATA,s->data,n)<0){good=0;break;}
    }
    if(!running || s->quit || s->aborted)good=0;
    if(listing)sceIoDclose(file);else if(sceIoClose(file)<0)good=0;
    close_socket(DATA);close_socket(PASSIVE);s->passive=0;
    if(upload){if(good && finish_upload(temp,path)<0)good=0;if(!good)sceIoRemove(temp);}
    if(!s->quit){reply(good?"226 Transfer complete.\r\n":"426 Transfer aborted or failed.\r\n");if(s->aborted)reply("226 Abort complete.\r\n");}
}
static void passive(int extended){
    close_socket(DATA);close_socket(PASSIVE);s->passive=0;
    SmTcpListen p={PASSIVE,0,s->peer};int rc=rpc(SM_TCP_LISTEN,&p,sizeof(p));
    if(rc<0 || s->reply.length!=sizeof(SmTcpAddress)){reply("425 Cannot listen.\r\n");return;}
    SmTcpAddress a;memcpy(&a,s->reply.payload,sizeof(a));s->passive=1;
    if(extended)reply("229 Entering Extended Passive Mode (|||%u|).\r\n",(unsigned)a.port);
    else {char ip[16];snprintf(ip,sizeof(ip),"%s",s->ip);for(char *c=ip;*c;c++)if(*c=='.')*c=',';
        reply("227 Entering Passive Mode (%s,%u,%u).\r\n",ip,(unsigned)(a.port>>8),(unsigned)(a.port&255));}
}
static void command(char *line){
    char *arg=strchr(line,' ');if(arg)*arg++=0;else arg=line+strlen(line);
    for(char *p=line;*p;p++)*p=upper(*p);
    if(strcmp(line,"RNTO") && strcmp(line,"RNFR"))s->rename[0]=0;
    if(!strcmp(line,"USER")){s->authenticated=0;s->user_ok=!strcmp(arg,username);reply("331 Password required.\r\n");}
    else if(!strcmp(line,"PASS")){
        s->authenticated=s->user_ok && !strcmp(arg,password);memset(arg,0,strlen(arg));
        if(s->authenticated)reply("230 Logged in.\r\n");else{reply("530 Login incorrect.\r\n");if(++s->failures>=5)s->quit=1;sceKernelDelayThread(500000);}
    }else if(!strcmp(line,"QUIT")){reply("221 Goodbye.\r\n");s->quit=1;}
    else if(!strcmp(line,"FEAT"))reply("211-Features\r\n UTF8\r\n EPSV\r\n SIZE\r\n MDTM\r\n REST STREAM\r\n MLST type*;size*;\r\n211 End\r\n");
    else if(!strcmp(line,"SYST"))reply("215 UNIX Type: L8\r\n");
    else if(!s->authenticated)reply("530 Login first.\r\n");
    else if(!strcmp(line,"NOOP"))reply("200 OK.\r\n");
    else if(!strcmp(line,"OPTS"))reply(ascii_equal(arg,"UTF8 ON")?"200 UTF8 enabled.\r\n":"501 Unsupported option.\r\n");
    else if(!strcmp(line,"TYPE"))reply(!strcmp(arg,"I") || !strcmp(arg,"A")?"200 Type accepted.\r\n":"504 Unsupported type.\r\n");
    else if(!strcmp(line,"PWD") || !strcmp(line,"XPWD")){
        char escaped[FTP_PATH*2];unsigned j=0;for(const char *p=s->cwd;*p;p++){escaped[j++]=*p;if(*p=='"')escaped[j++]='"';}escaped[j]=0;
        reply("257 \"%s\"\r\n",escaped);
    }else if(!strcmp(line,"PASV"))passive(0);
    else if(!strcmp(line,"EPSV")){if(!*arg || !strcmp(arg,"1"))passive(1);else reply("522 IPv4 only.\r\n");}
    else if(!strcmp(line,"ABOR")){close_socket(DATA);close_socket(PASSIVE);s->passive=0;reply("226 No active transfer.\r\n");}
    else if(!strcmp(line,"REST")){
        unsigned n;
        if(decimal(arg,UINT32_MAX,&n)<0)reply("501 Invalid offset.\r\n");
        else{s->restart=n;reply("350 Download restart accepted.\r\n");}
    }else if(!strcmp(line,"LIST") || !strcmp(line,"NLST") || !strcmp(line,"MLSD") || !strcmp(line,"RETR") || !strcmp(line,"STOR"))transfer(line,arg);
    else {
        char virtual_path[FTP_PATH],path[FTP_PATH+4];SceIoStat st;int rc=-1;
        if(!strcmp(line,"CDUP"))arg="..";
        if(path_for(arg,virtual_path,path)<0){reply("550 Invalid path.\r\n");return;}
        if(!strcmp(line,"CWD") || !strcmp(line,"CDUP")){
            rc=sceIoGetstat(path,&st);if(rc>=0 && FIO_S_ISDIR(st.st_mode)){strcpy(s->cwd,virtual_path);reply("250 Directory changed.\r\n");}else reply("550 Not a directory.\r\n");
        }else if(!strcmp(line,"SIZE") || !strcmp(line,"MDTM")){
            if(sceIoGetstat(path,&st)<0 || FIO_S_ISDIR(st.st_mode)){reply("550 Not a file.\r\n");return;}
            if(!strcmp(line,"SIZE")){char size_text[21];ftp_size_text((uint64_t)st.st_size,size_text);reply("213 %s\r\n",size_text);}
            else reply("213 %04u%02u%02u%02u%02u%02u\r\n",st.sce_st_mtime.year,st.sce_st_mtime.month,st.sce_st_mtime.day,st.sce_st_mtime.hour,st.sce_st_mtime.minute,st.sce_st_mtime.second);
        }else if(!strcmp(line,"RNFR")){
            s->rename[0]=0;
            if(read_only || !strcmp(virtual_path,"/") || sceIoGetstat(path,&st)<0)reply("550 Rename unavailable.\r\n");
            else{strcpy(s->rename,virtual_path);reply("350 Send RNTO.\r\n");}
        }else if(!strcmp(line,"DELE") || !strcmp(line,"RMD") || !strcmp(line,"MKD") || !strcmp(line,"RNTO")){
            if(read_only || !strcmp(virtual_path,"/")){reply("550 Write denied.\r\n");return;}
            if(!strcmp(line,"DELE"))rc=sceIoRemove(path);
            else if(!strcmp(line,"RMD"))rc=sceIoRmdir(path);
            else if(!strcmp(line,"MKD"))rc=sceIoMkdir(path,0777);
            else if(s->rename[0]){char old[FTP_PATH+4];snprintf(old,sizeof(old),"ms0:%s",s->rename);rc=sceIoRename(old,path);s->rename[0]=0;}
            reply(rc<0?"550 Operation failed.\r\n":"250 Operation complete.\r\n");
        }else reply("502 Command not implemented.\r\n");
    }
}
static int worker(SceSize size,void *arg){
    (void)size;(void)arg;
    /* Never perform filesystem I/O on the module manager's startup thread.
     * Let XMB and the resident USB loader settle before allocating or polling. */
    for(unsigned i=0;i<200 && running;i++)sceKernelDelayThread(100000);
    if(!running)return 0;
    status("FTP 0.3: delayed worker started; allocating user buffers",0);
    memory=sceKernelAllocPartitionMemory(2,"FTP user buffers",PSP_SMEM_High,sizeof(Session),NULL);
    if(memory<0){status("User buffer allocation failed",memory);running=0;return 0;}
    s=sceKernelGetBlockHeadAddr(memory);memset(s,0,sizeof(*s));s->serial=(unsigned)clock_us();
    status("FTP 0.3: reading INI",0);
    if(config()<0 || !enabled){status("Disabled or missing valid INI credentials",0);goto cleanup;}
    status("FTP 0.3: waiting for resident USB bridge",0);
    unsigned long long next_connect=0;
    while(running){
        if(sceIoDevctl("stm:",SM_DEV_RESIDENT,NULL,0,NULL,0)!=SM_RESIDENT_MAGIC || sceIoDevctl("stm:",SM_DEV_STATUS,NULL,0,NULL,0)<=0){sceKernelDelayThread(500000);continue;}
        int rc=rpc(SM_CAPABILITIES,NULL,0);uint32_t caps=0;
        if(rc>=0 && s->reply.length==4)memcpy(&caps,s->reply.payload,4);
        if(!(caps&SM_CAP_TCP_SERVER)){status("FTP firmware capability missing",rc);sceKernelDelayThread(2000000);continue;}
        rc=rpc(SM_INFO,NULL,0);SmInfo info;
        if(rc<0 || s->reply.length!=sizeof(info)){sceKernelDelayThread(500000);continue;}
        memcpy(&info,s->reply.payload,sizeof(info));
        if(info.wifi_state!=SM_WIFI_READY){
            if(clock_us()>=next_connect){rpc(SM_CONNECT,NULL,0);next_connect=clock_us()+30000000;status("Waiting for saved ESP Wi-Fi profile",0);}
            sceKernelDelayThread(500000);continue;
        }
        snprintf(s->ip,sizeof(s->ip),"%.15s",info.ip);rpc(SM_TCP_RESET,NULL,0);
        SmTcpListen listen={LISTENER,port,0};rc=rpc(SM_TCP_LISTEN,&listen,sizeof(listen));
        if(rc<0){status("FTP listen failed",rc);sceKernelDelayThread(1000000);continue;}
        char line[96];snprintf(line,sizeof(line),"FTP ready at %s:%d (ms0 only)",s->ip,port);status(line,0);
        while(running){
            SmTcpAccept accept={LISTENER,CONTROL};rc=rpc(SM_TCP_ACCEPT,&accept,sizeof(accept));
            if(rc==SM_BUSY){sceKernelDelayThread(100000);continue;}
            if(rc<0 || s->reply.length!=sizeof(SmTcpAddress))break;
            SmTcpAddress peer;memcpy(&peer,s->reply.payload,sizeof(peer));s->peer=peer.peer;
            s->used=s->quit=s->authenticated=s->user_ok=s->passive=s->failures=0;s->restart=0;s->rename[0]=0;strcpy(s->cwd,"/");
            reply("220 PSPStreamFTP - Memory Stick only.\r\n");
            unsigned long long deadline=clock_us()+(unsigned long long)idle_seconds*1000000;
            unsigned long long next_accept=0;
            while(running && !s->quit){
                char *cmd=s->command_text;int n=command_line(cmd,sizeof(s->command_text));
                if(n<0)break;
                if(n){command(cmd);memset(cmd,0,sizeof(s->command_text));deadline=clock_us()+(unsigned long long)idle_seconds*1000000;}
                else if(clock_us()>=deadline){reply("421 Idle timeout.\r\n");break;}
                else {
                    if(clock_us()>=next_accept){refuse_extra_connection();next_accept=clock_us()+1000000;}
                    sceKernelDelayThread(10000);
                }
            }
            close_socket(DATA);close_socket(PASSIVE);close_socket(CONTROL);
        }
        rpc(SM_TCP_RESET,NULL,0);sceKernelDelayThread(500000);
    }
    /* Firmware also expires listeners after 20 s without commands. */
cleanup:
    memset(s,0,sizeof(*s));sceKernelFreePartitionMemory(memory);memory=-1;s=NULL;
    memset(password,0,sizeof(password));running=0;return 0;
}
int module_start(SceSize size,void *arg){
    (void)size;(void)arg;
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_VSH)return 0;
    running=1;thread=sceKernelCreateThread("PSP FTP service",worker,0x38,8192,0,NULL);
    if(thread<0){running=0;return thread;}
    int rc=sceKernelStartThread(thread,0,NULL);
    if(rc<0){sceKernelDeleteThread(thread);thread=-1;running=0;}return rc;
}
int module_stop(SceSize size,void *arg){
    (void)size;(void)arg;running=0;
    if(thread>=0){SceUInt timeout=12000000;if(sceKernelWaitThreadEnd(thread,&timeout)<0)return -1;sceKernelDeleteThread(thread);thread=-1;}
    return 0;
}
