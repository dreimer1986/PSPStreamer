import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class WifiConnectionTests(unittest.TestCase):
    def test_retry_state_machine(self):
        source = r'''
#include <assert.h>
#define PSP_NET_MODULE_COMMON 0
#define PSP_NET_MODULE_INET 1
#define PSP_NETWORK_PROFILE 0
#define PSP_NET_APCTL_STATE_GOT_IP 4
static const char *failure_step;
static int active_network_profile;
static int calls[5],fail_stage=-1,state,ticks,connects,disconnects;
static int stuck,join_after=2,cancel_after,profile_exists=1,read_error,connect_error;
static int terminated;
static void recovery_log(const char *e,int r,int s,const char *p){(void)e;(void)r;(void)s;(void)p;}
static int sceNetApctlTerm(void){terminated++;state=0;stuck=0;return 0;}
static int init(int stage){calls[stage]++;return stage==fail_stage?-99:0;}
static int sceUtilityLoadNetModule(int x){return init(x);}
static int sceNetInit(int a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;return init(2);}
static int sceNetInetInit(void){return init(3);}
static int sceNetApctlInit(int a,int b){(void)a;(void)b;return init(4);}
static int sceUtilityCheckNetParam(int p){return profile_exists && p==2?0:-1;}
static int sceNetApctlGetState(int *out){*out=state;return read_error;}
static int sceNetApctlDisconnect(void){disconnects++;if(!stuck)state=0;return 0;}
static int sceNetApctlConnect(int p){assert(p==2);connects++;state=1;return connect_error;}
static int wifi_wait_tick(void){ticks++;if(join_after && ticks>=join_after)state=4;return cancel_after && ticks>=cancel_after;}
#include "wifi_connection.h"
int main(void){
    fail_stage=3;
    assert(wifi_initialize()==-99 && wifi_init_stage==3);
    fail_stage=-1;
    assert(wifi_associate(0)==0 && active_network_profile==2);
    assert(calls[0]==1 && calls[1]==1 && calls[2]==1 && calls[3]==2 && calls[4]==1);
    assert(connects==1);
    assert(wifi_associate(0)==0 && connects==1 && disconnects==0);
    ticks=0;assert(wifi_associate(1)==0 && connects==2 && disconnects==1);
    state=2;ticks=0;assert(wifi_associate(0)==0 && disconnects==2 && connects==3);
    state=2;stuck=1;join_after=0;ticks=0;
    assert(wifi_associate(0)==-6 && ticks==100 && connects==3);
    stuck=0;state=0;ticks=0;
    assert(wifi_associate(0)==-3 && ticks==300 && state==0 && connects==4);
    ticks=0;cancel_after=3;
    assert(wifi_associate(0)==-5 && ticks==3 && state==0);
    cancel_after=0;join_after=2;ticks=0;
    assert(wifi_associate(0)==0 && state==4);
    read_error=-123;assert(wifi_associate(1)==-123);read_error=0;
    profile_exists=0;assert(wifi_associate(1)==-4);profile_exists=1;
    state=0;connect_error=-77;assert(wifi_associate(0)==-77);
    assert(calls[0]==1 && calls[4]==1);
    connect_error=0;state=4;stuck=1;join_after=0;ticks=0;
    assert(wifi_associate(1)==-6 && wifi_rebuild_pending && ticks==100);
    ticks=0;
    assert(wifi_associate(0)==-6 && wifi_rebuild_pending && ticks==100 && !terminated);
    wifi_allow_apctl_restart=1;join_after=101;ticks=0;
    assert(wifi_associate(0)==0 && terminated==1 && !wifi_rebuild_pending);
    assert(calls[0]==1 && calls[1]==1 && calls[2]==1 && calls[3]==2 && calls[4]==2);
    state=4;stuck=1;join_after=0;ticks=0;cancel_after=3;
    assert(wifi_associate(1)==-5 && wifi_rebuild_pending && terminated==1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'wifi.c'
            path.write_text(source)
            binary = root / 'wifi'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-I', str(ROOT/'psp-client'),
                            str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
