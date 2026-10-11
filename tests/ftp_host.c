#define PSP_FTP_HOST_TEST 1
#include "../psp-ftp/main.c"
int sm_network_state(void){return SM_WIFI_READY;}
int64_t esp_timer_get_time(void){return sceKernelGetSystemTimeWide();}
int main(int argc,char **argv){
    char text[21];
    ftp_size_text(UINT64_MAX,text);if(strcmp(text,"18446744073709551615"))return 4;
    ftp_size_text(0,text);if(strcmp(text,"0"))return 4;
    ftp_size_text(131,text);if(strcmp(text,"131"))return 4;
    if(argc!=2 || chdir(argv[1]))return 2;
    running=1;return worker(0,NULL);
}
