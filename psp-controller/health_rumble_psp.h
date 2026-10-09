#include "health_profiles.h"
static HealthProfiles health_profiles;
static HealthRumble health_states[HR_PROFILE_MAX];
static int health_enabled;
static void health_load(void) {
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME)return;
    health_profiles_init(&health_profiles);health_enabled=0;
    char id[17]={0},line[384],chunk[256];int used=0,total=0,n=0,rc=0;
    SceGameInfo *info=sceKernelGetGameInfo();if(info)memcpy(id,info->title_id,16);
    const char *path=sceKernelInitFileName();if(!path)path="";
    int fd=sceIoOpen("ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer-rumble.ini",PSP_O_RDONLY,0);
    if(fd<0)return;
    while((n=sceIoRead(fd,chunk,sizeof(chunk)))>0) {
        if((total+=n)>65536){rc=-1;break;}
        for(int i=0;i<n;i++) {
            if(chunk[i]=='\n') {
                line[used]=0;used=0;
                if(health_profiles_line(&health_profiles,line,id,path)<0){rc=-1;break;}
            } else if(!chunk[i] || used==(int)sizeof(line)-1){rc=-1;break;}
            else line[used++]=chunk[i];
        }
        if(rc<0)break;
    }
    if(sceIoClose(fd)<0 || n<0)rc=-1;
    if(rc>=0 && used){line[used]=0;rc=health_profiles_line(&health_profiles,line,id,path);}
    if(rc>=0){health_profiles_finish(&health_profiles);if(health_profiles.overflow)rc=-1;}
    if(rc<0)health_profiles.count=0;
    for(int i=0;i<health_profiles.count;i++)health_enabled|=health_profiles.config[i][HR_ENABLED];
    controller_log("health rumble matched rules (negative = invalid)",rc<0?rc:health_profiles.count);
}
static int health_read(uint32_t address,unsigned bytes,uint32_t *value,void *ctx) {
    (void)ctx;if(!hr_range(address,bytes))return 0;
    int intr=sceKernelCpuSuspendIntr();
    if(bytes==1)*value=*(volatile uint8_t *)address;
    else if(bytes==2)*value=*(volatile uint16_t *)address;
    else *value=*(volatile uint32_t *)address;
    sceKernelCpuResumeIntr(intr);return 1;
}
static void health_publish(unsigned long long now,int active) {
    unsigned strength=health_profiles_step(&health_profiles,health_states,now,active,health_read,NULL);
    rumble_publish(0,strength,now);
}
