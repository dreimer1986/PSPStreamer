#include "health_rumble.h"
static int health_config[HR_COUNT]={0,0,2,0,0,0,100,180,120,200,0,1,0,120,60,500};
static HealthRumble health_state;
static const TitleRuleKey health_keys[]={
    {"enabled",0,1},{"address",0,0x09ffffff},{"type",1,4},
    {"pointer",0,1},{"offset",0,65535},{"minimum",0,2147483647},
    {"maximum",1,2147483647},{"strength",0,255},{"duration_ms",10,1000},
    {"cooldown_ms",20,5000},{"gate_address",0,0x09ffffff},{"gate_value",0,2147483647},
    {"dynamic",0,1},{"damage_peak",1,2147483647},{"duration_min_ms",10,1000},{"duration_max_ms",10,1000}
};
static void health_load(void) {
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME)return;
    int rc=title_rules_load("ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer-rumble.ini",
        sceKernelInitFileName(),health_keys,HR_COUNT,health_config);
    if(rc<=0)health_config[HR_ENABLED]=0;
    controller_log("health rumble profile section (negative = invalid)",rc);
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
    unsigned strength=hr_step(&health_state,health_config,now,active,health_read,NULL);
    rumble_publish(0,strength,now);
}
