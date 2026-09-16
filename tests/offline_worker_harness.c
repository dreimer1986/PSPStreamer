/* Appended to offline_http_harness's native transport and actual downloader. */
#include <mbedtls/sha256.h>
typedef int SceSize;
typedef int SceUID;
typedef struct {unsigned int freeClusters,sectorSize,sectorCount;} SceDevInf;
typedef struct {SceDevInf *dev_inf;} SceDevctlCmd;
#define PSP_O_RDONLY O_RDONLY
#define PSP_O_TRUNC O_TRUNC
#define SCE_PR_GETDEV 1
#define sceIoRead read
#define sceIoRemove unlink
#define sceIoRename rename
#define TXT_DOWNLOAD_SPACE 0
#define TXT_WIFI_NOT_READY 1
#define PSP_NET_APCTL_STATE_GOT_IP 4
static int active_network_profile=1,download_catalog_request;
static char response[512*1024];
static int sceNetApctlGetState(int *state){*state=4;return 0;}
static int sceNetApctlConnect(int profile){(void)profile;return 0;}
static volatile int download_done,download_result,download_percent,download_stage;
static char download_key[33],download_post[1024],download_reply[8192],root[256];
#define OFFLINE_ROOT root
static int no_space;
static const char *tr(int id){(void)id;return "no space";}
static void sceKernelDelayThread(int us){struct timespec t={us/1000000,(us%1000000)*1000};nanosleep(&t,NULL);}
static int sceIoMkdir(const char *path,int mode){return !strncmp(path,"ms0:",4)?0:mkdir(path,mode);}
static int sceIoDevctl(const char *dev,int op,SceDevctlCmd *cmd,int size,void *out,int bytes){
    (void)dev;(void)op;(void)size;(void)out;(void)bytes;
    *cmd->dev_inf=(SceDevInf){no_space==1?0:10000000,512,8};return 0;
}
/* JSON_HELPERS */
/* FILE_HELPERS */
/* HASH_AND_WORKER */
int main(int argc,char **argv){
    assert(argc==5);server_port=atoi(argv[1]);strcpy(download_key,argv[2]);strcpy(root,argv[3]);no_space=atoi(argv[4]);
    assert(offline_download_worker(0,NULL)==0);assert(download_done);
    if(no_space==1)assert(download_result<0 && strstr(download_error,"no space"));
    else if(no_space==2)assert(download_result<0 && strstr(download_error,"SHA256 mismatch"));
    else if(download_result){fprintf(stderr,"download error: %s\n",download_error);abort();}
    return 0;
}
