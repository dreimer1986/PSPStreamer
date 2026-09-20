import subprocess
import tempfile
import unittest
from pathlib import Path

from psp_streamer.server import Library, MediaItem

ROOT = Path(__file__).resolve().parents[1]


class MaintenanceTests(unittest.TestCase):
    def test_bounded_exit_handshake_and_dependency_teardown(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'exit-policy'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-client'),
                            str(ROOT / 'tests/exit_policy_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        code = (ROOT / 'psp-client/main.c').read_text()
        callback = code[code.index('int exit_callback('):code.index('int callback_thread(')]
        self.assertLess(callback.index('prepare_oc_exit()'), callback.index('sceKernelExitGame()'))
        tail = code[code.index('int browser_stopped=exit_join_worker'):]
        self.assertLess(tail.index('prepare_oc_exit()'), tail.index('sceNetApctlTerm()'))
        self.assertIn('if(browser_stopped)', tail)
        loop = code[code.index('playback_clock_idle();'):]
        self.assertLess(loop.index('pad.Buttons & PSP_CTRL_START'), loop.index('if(library_pending)'))
        self.assertLess(loop.index('pad.Buttons & PSP_CTRL_START'), loop.index('!browser_remote_stop()'))

    def test_oc_driver_published_after_clock_startup_and_joined_before_removal(self):
        code = (ROOT / 'psp-overclock/main.c').read_text()
        worker = code[code.index('static int thread_main('):code.index('int module_start(')]
        start = code[code.index('int module_start('):code.index('int module_stop(')]
        stop = code[code.index('int module_stop('):]
        self.assertNotIn('sceIoAddDrv', start)
        self.assertLess(worker.index('snapshot("startup_result")'), worker.index('sceIoAddDrv'))
        self.assertLess(worker.index('sceIoAddDrv'), worker.index('control_ready=control_registered'))
        self.assertLess(stop.index('running=0'), stop.index('sceKernelWaitThreadEnd'))
        self.assertLess(stop.index('sceKernelWaitThreadEnd'), stop.index('sceIoDelDrv'))

    def test_oc_ratio_steps_timeout_and_unexpected_readback(self):
        source = r'''
#include <assert.h>
static unsigned CTL,writes[8];static int count,waits,settles,fail_at,bad_readback;
#define SYNC() ((void)0)
static void oc_ratio_write(unsigned value){assert(count<8);writes[count++]=value;CTL=value;}
static int ready(void){waits++;if(waits==fail_at)return -1;CTL&=~0x80U;if(bad_readback)CTL=(CTL&~15U)|2;return 0;}
static void settle(void){settles++;}
#include "ratio_transition.h"
int main(void){
    CTL=0xab03;assert(oc_ratio_to_five(0,0)==0);
    assert(count==3&&waits==3&&settles==3&&CTL==0xab05);
    assert(writes[0]==0xab83&&writes[1]==0xab84&&writes[2]==0xab85);
    count=waits=settles=0;CTL=4;assert(oc_ratio_to_five(0,0)==0&&count==2);
    count=waits=settles=0;CTL=5;assert(oc_ratio_to_five(0,0)==0&&!count&&!waits&&!settles);
    for(unsigned i=0;i<16;i++)if(i<3||i>5){CTL=i;assert(oc_ratio_to_five(0,0)==-4&&!count);}
    for(fail_at=1;fail_at<=3;fail_at++){
        CTL=3;count=waits=settles=0;
        assert(oc_ratio_to_five(0,0)==-1&&count==fail_at&&settles==fail_at-1);
    }
    fail_at=0;CTL=3;count=waits=settles=0;bad_readback=1;
    assert(oc_ratio_to_five(0,0)==-3&&count==1&&!settles);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'ratio'
            subprocess.run(['cc', '-x', 'c', '-', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-I', str(ROOT / 'psp-overclock'),
                            '-o', str(binary)], input=source, text=True, check=True)
            subprocess.run([str(binary)], check=True)

    def test_oc_unlimited_enforcement_only_bypasses_conflict_count(self):
        import re
        code = (ROOT / 'psp-overclock/main.c').read_text()
        expression = re.search(r'if\(([^\n]+)\)\{enabled=0;status="clock conflict:', code).group(1)
        app_guard = re.search(r'if\((control_active[^\n]+)\)continue;', code).group(1)
        source = '''#include <assert.h>
static int limit(int enforce_unlimited, unsigned *count) {
    unsigned conflicts=*count;
    int stop=(''' + expression + ''');
    *count=conflicts;return stop;
}
static int enforce_allowed(int control_active,int enforce_unlimited,int enforce) {
    if (''' + app_guard + ''') return 0;
    return enforce;
}
int main(void){
    assert(enforce_allowed(0,0,1));
    assert(!enforce_allowed(1,0,1));
    assert(enforce_allowed(1,1,1));
    assert(!enforce_allowed(1,1,0));
    assert(!enforce_allowed(0,1,0));
    unsigned count=0;
    for(int i=0;i<3;i++)assert(!limit(0,&count));
    assert(limit(0,&count));
    for(int i=0;i<10000;i++)assert(!limit(1,&count));
    assert(limit(0,&count));return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'limit'
            subprocess.run(['cc', '-x', 'c', '-', '-Wall', '-Wextra', '-Werror',
                            '-o', str(binary)], input=source, text=True, check=True)
            subprocess.run([str(binary)], check=True)
        self.assertLess(code.index('if(!enforce){'), code.index('if(!enforce_unlimited'))
        self.assertLess(code.index('if(control_active && !enforce_unlimited)'), code.index('snapshot("clock_mismatch")'))
        self.assertIn('if(!running || suspended)return -2;',
                      (ROOT / 'psp-overclock/clock_transition.h').read_text())
        self.assertIn('if(r)enabled=0;', code)

    def test_optional_app_clock_profiles_and_lcd_idle(self):
        code = (ROOT / 'psp-client/main.c').read_text()
        self.assertNotIn('playback_clock_release();', code)
        self.assertIn('playback_clock_idle();', code)
        offline = (ROOT / 'psp-client/offline_ui.h').read_text()
        self.assertIn('playback_clock_idle();', offline)
        self.assertNotIn('playback_clock_release();', offline)
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'power'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-client'),
                            str(ROOT / 'tests/power_policy_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_oc_optional_control_permission_and_limits(self):
        code = (ROOT / 'psp-overclock/main.c').read_text()
        code = code[code.index('static int control_devctl('):code.index('static int control_init(')]
        source = r'''
#include <assert.h>
#include <stddef.h>
#include "control_api.h"
typedef int PspIoDrvFileArg;
static int target=333,configured_target=443,control_ready,app_control,enabled,suspended,running=1;
static int control_pending=-1,control_result,control_active;
static int exit_requested,worker_exit_done,exit_result;
static unsigned CTL,MUL,CPU;
static int sceKernelGetModel(void){return 2;}
static int oc_supported_model(int m){return m==2;}
static unsigned oc_khz(unsigned a,unsigned b,unsigned c){(void)a;(void)b;(void)c;return 442150;}
static int scePowerGetCpuClockFrequencyInt(void){return 333;}
static int sceKernelCpuSuspendIntr(void){return 0;}
static void sceKernelCpuResumeIntr(int i){(void)i;}
''' + code + r'''
static int call(unsigned cmd){return control_devctl(NULL,"",cmd,NULL,0,NULL,0);}
int main(void){
    assert(call(OC_CMD_CPU_KHZ)==442150);
    assert(call(OC_CMD_TARGET)==333);
    assert(call(OC_CMD_SET|222)<0 && control_pending==-1);
    control_ready=app_control=enabled=1;
    assert(call(OC_CMD_STATUS)==0&&!control_active);
    assert(call(OC_CMD_SET|65)<0);
    assert(call(OC_CMD_SET|472)<0);
    assert(call(OC_CMD_SET|444)<0&&!control_active);
    assert(call(OC_CMD_SET|66)==0 && control_pending==66&&control_active);
    assert(call(OC_CMD_STATUS)==1);
    control_pending=-1;
    assert(call(OC_CMD_SET|443)==0 && control_pending==443);
    assert(call(OC_CMD_SET)==0 && control_pending==0&&control_active);
    suspended=1;assert(call(OC_CMD_SET|222)<0);
    suspended=0;enabled=0;assert(call(OC_CMD_SET|222)<0);
    enabled=1;app_control=0;assert(call(OC_CMD_SET|222)<0);
    app_control=1;
    assert(control_devctl(NULL,"",OC_CMD_SET|222,&target,4,NULL,0)<0);
    assert(control_devctl(NULL,"",OC_CMD_STATUS,NULL,0,&target,4)<0);
    assert(call(OC_CMD_EXIT_STATUS)<0);
    app_control=0;assert(call(OC_CMD_PREPARE_EXIT)<0&&running);
    app_control=1;assert(call(OC_CMD_PREPARE_EXIT)==0&&!running&&!control_ready);
    assert(call(OC_CMD_EXIT_STATUS)==1&&control_pending==-1);
    assert(call(OC_CMD_SET|433)<0);
    worker_exit_done=1;assert(call(OC_CMD_EXIT_STATUS)==0);
    exit_result=-3;assert(call(OC_CMD_EXIT_STATUS)==-3);
    assert(call(OC_CMD_PREPARE_EXIT)==0&&call(OC_CMD_EXIT_STATUS)==-3);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'control.c'
            path.write_text(source)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'), str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_oc_overlay_pixel_formats_and_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'overlay'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'),
                            str(ROOT / 'tests/oc_overlay_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_oc_event_history_append_and_io_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'report'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'),
                            str(ROOT / 'tests/oc_report_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        code = (ROOT / 'psp-overclock/main.c').read_text()
        callback = code[code.index('static int power_callback('):code.index('static int thread_main(')]
        self.assertNotIn('snapshot(', callback)
        self.assertNotIn('sceIo', callback)
        snapshot = code[code.index('static void snapshot('):code.index('static int matches(void);')]
        self.assertIn('if(!report || suspended==1)return;', snapshot)
        self.assertIn('oc_report_write(path,text,n,1)', snapshot)
        self.assertIn('oc_report_write(path,text,n,0)', snapshot)
        for event in ('session_start', 'startup_result', 'clock_mismatch', 'reapply_result',
                      'suspend_resume', 'manual_snapshot', 'session_stopping', 'session_end'):
            self.assertIn('snapshot("' + event + '")', code)

    def test_oc_config_parser_atomic_and_whitespace_tolerant(self):
        source = r'''
#include <assert.h>
#include <stdio.h>
#include "config_parse.h"
static int parse(const char *input, OcConfig *out, int *keys, int *line) {
    char text[1024]; size_t n=strlen(input);
    assert(n<sizeof(text)); memcpy(text,input,n+1);
    return oc_config_parse(text,(int)n,out,keys,line);
}
int main(int argc,char **argv) {
    OcConfig c={0,333,0,1,1,1,0}; int keys,line;
    assert(parse("# header\nenabled=1\ntarget_mhz=383\nenforce=1\nreport=1\n",&c,&keys,&line)==0);
    assert(c.enabled==1 && c.target==383 && c.enforce==1 && c.report==1 && keys==4 && !line);
    assert(c.overlay==1 && c.app_control==1 && !c.enforce_unlimited);
    assert(parse("enforce_unlimited=1\napp_control=0\noverlay=0",&c,&keys,&line)==0);
    assert(c.enforce_unlimited==1&&!c.enforce&&!c.app_control&&!c.overlay);
    assert(parse("overlay=1",&c,&keys,&line)==0 && c.overlay==1 && !c.enabled);
    assert(parse("\xef\xbb\xbf  enabled = 0\r\n\ttarget_mhz = 443 ; note\r\nreport=0",&c,&keys,&line)==0);
    assert(!c.enabled && c.target==443 && !c.report && !c.enforce && keys==3);
    const char *bad[]={"", "# no settings\n", "enabled=1\ntarget_mhz=999", "enabled=2",
        "enabled=1\nreport=0\ntarget_mhz=65", "target_mhz=9999999999999999999999",
        "enabled=", "enforce=-1", "report=1oops", "typo=1", "enabled 1", "overlay=2", "enforce_unlimited=2"};
    for(unsigned int i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        OcConfig before=c;
        assert(parse(bad[i],&c,&keys,&line)<0);
        assert(!memcmp(&before,&c,sizeof(c))); /* never partially enable */
    }
    char nul[]="enabled=1\0target_mhz=443";
    assert(oc_config_parse(nul,sizeof(nul)-1,&c,&keys,&line)<0);
    if(argc==2) {
        char file[1024]; FILE *f=fopen(argv[1],"rb");assert(f);
        int n=(int)fread(file,1,sizeof(file)-1,f);fclose(f);file[n]=0;
        assert(oc_config_parse(file,n,&c,&keys,&line)==0 && keys==7);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'config.c'
            path.write_text(source)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'), str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary), str(ROOT / 'psp-overclock/StreamerOC.ini.example')], check=True)

    def test_oc_power_callback_slot_fallback(self):
        source = '''
#include <assert.h>
static int automatic, available, calls;
static int scePowerRegisterCallback(int slot, int callback) {
    assert(callback==123);
    if(calls++==0) { assert(slot==-1); return automatic; }
    assert(slot==16-(calls-1)); /* bounded descending scan, no stolen slots */
    return slot==available?0:-42;
}
#include "power_callback_slot.h"
int main(void) {
    int a,r;
    automatic=0;calls=0;
    assert(oc_register_power_callback(123,&a,&r)==0 && calls==1);
    automatic=7;calls=0;
    assert(oc_register_power_callback(123,&a,&r)==7 && a==7 && r==7 && calls==1);
    automatic=-99;
    for(available=15;available>=0;available--) {
        calls=0;
        assert(oc_register_power_callback(123,&a,&r)==available);
        assert(a==-99 && r==0 && calls==17-available);
    }
    calls=0;available=-1;
    assert(oc_register_power_callback(123,&a,&r)==-1);
    assert(a==-99 && r==-42 && calls==17);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'callback.c'
            path.write_text(source)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'), str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_old_launcher_delegates_and_calibration_is_gone(self):
        import server
        from psp_streamer.server import main
        self.assertIs(server.main, main)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.mkv').touch()
            library = Library([root])
            self.assertEqual([v['name'] for v in library.browse(0, '')['videos']], ['test.mkv'])
            for index in (-1, 1, 999):
                with self.assertRaises(ValueError):
                    library.decode(library.encode(MediaItem(index, 'test.mkv')))

    def test_oc_math_not_sony_clock_or_config_echo(self):
        source = '''
#include <assert.h>
#include "clock_math.h"
int main(void) {
    assert(oc_supported_model(2)); assert(!oc_supported_model(10));
    assert(oc_numerator(333)==180);
    assert(oc_numerator(443)==239);
    assert(oc_khz(5,(180<<8)|20,0x01ff01ff)==333000);
    assert(oc_khz(5,(239<<8)|20,0x01ff01ff)==442150);
    assert(oc_khz(5,(239<<8)|20,0x01000200)==0); /* invalid zero 9-bit denominator */
    assert(oc_khz(5,(239<<8)|20,0x01000100)==442150);
    assert(oc_khz(5,(239<<8)|20,0x00800100)==221075);
    assert(oc_khz(0x85,(239<<8)|20,0x01ff01ff)==0); /* PLL changing */
    assert(oc_khz(4,(239<<8)|20,0x01ff01ff)==0); /* unknown PLL ratio */
    assert(oc_khz(5,0,0x01ff01ff)==0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'math.c'
            path.write_text(source)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'), str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_oc_actual_ramp_abort_and_restore_with_mock_registers(self):
        source = (ROOT / 'psp-overclock/main.c').read_text()
        transitions = (ROOT / 'psp-overclock/clock_transition.h').read_text()
        self.assertNotIn('scePowerSetClockFrequency', source + transitions)
        self.assertIn('mfc0 %0,$12', source)
        self.assertIn('mtc0 %1,$12', source)
        harness = '''
#include <assert.h>
#include <stdlib.h>
#include "clock_math.h"
static unsigned int CTL=5,MUL=0x01240901,CPU=0x01ff01ff,BUS=0x01ff01ff;
#include <string.h>
static const char *mutate_at;
static int running=1,suspended,changed,target=443,fail_ready,interfere,yields;
static int report=1,snapshots,locked,fail_ratio,multiplier_calls;
static int sony_mode,sony_calls,sony_baseline_result;
typedef int OcClockGuard;
static OcClockGuard oc_clock_lock(void){assert(!locked);locked=1;return 1;}
static void oc_clock_unlock(OcClockGuard g){assert(g==1&&locked);locked=0;}
static void snapshot(const char *event){
    assert(event&&!locked);snapshots++;
    if(interfere==4)CPU=0x00800100;
    if(mutate_at&&!strcmp(event,mutate_at))CPU=0x00800100;
}
#define SYNC() ((void)0)
static void settle(void){assert(locked);}
static int ready(void){
    assert(locked);
    if(fail_ratio&&(CTL&0x80)&&(CTL&15)==(unsigned)fail_ratio)return -1;
    CTL&=~0x80;return fail_ready?-1:0;
}
static void multiplier(unsigned int n){assert(locked&&(CTL&0x8f)==5);multiplier_calls++;MUL=(MUL&0xffff0000)|(n<<8)|OC_DEN;}
static unsigned ratio_writes[8],ratio_multipliers[8];static int ratio_count;
static void oc_ratio_write(unsigned int n){
    assert(locked&&ratio_count<8);ratio_multipliers[ratio_count]=MUL;
    ratio_writes[ratio_count++]=n;CTL=n;
}
#include "ratio_transition.h"
static void sceKernelDelayThreadCB(int n) {
    assert(n==10000&&!locked);yields++;
    if(interfere==1)CPU=0x00800100;
    if(interfere==2)suspended=1;
    if(interfere==3)running=0;
}
#include "clock_transition.h"
static int scePowerSetClockFrequency(int pll,int cpu,int bus) {
    assert(!locked && pll==333 && cpu==333 && bus==166);sony_calls++;
    if(sony_mode==1)return 0; /* ARK successful no-op. */
    if(sony_mode==2)return -77;
    CTL=5;MUL=0x01240901;CPU=0x01ff01ff;BUS=0x00ff01ff;
    if(sony_mode==3)suspended=1;
    if(sony_mode==4)running=0;
    return 0;
}
#include "sony_startup.h"
int main(void) {
    assert(oc_supported_model(2));
    CPU=BUS=0x00800100;
    assert(apply()==0&&changed&&matches()&&yields==59);
    assert(oc_khz(CTL,MUL,CPU)==442150);
    assert(restore()==0&&((MUL>>8)&255)==180);
    target=333;assert(apply()==0&&matches());
    assert(CPU==0x01ff01ff&&BUS==0x01ff01ff);

    /* Every supported low profile uses real registers, never Sony's cache. */
    for(target=66;target<=333;target++) {
        assert(apply()==0&&matches());
        assert(abs((int)oc_khz(CTL,MUL,CPU)-target*1000)<1000);
        if(target<333)assert(abs((int)oc_khz(CTL,MUL,BUS)-target*500)<1000);
    }
    target=433;assert(apply()==0&&matches());
    target=133;assert(apply()==0&&matches());
    target=433;assert(apply()==0&&matches());
    interfere=4;assert(apply()==-3); /* Foreign write during report I/O. */
    interfere=0;changed=0;assert(apply()==0&&matches());
    unsigned int saved=CPU;
    CPU=0x00800100;
    assert(restore()==-3&&apply()==-3); /* No foreign state overwritten. */
    CPU=saved;assert(restore()==0);
    target=443;interfere=1;yields=0;
    assert(apply()==-3&&yields==1&&!locked);
    assert(restore()==-3);
    interfere=0;changed=0;suspended=1;assert(apply()==-2);
    suspended=0;fail_ready=1;assert(apply()==-1&&!locked);

    /* Exact startup register tuple from the hard-power-off session. */
    fail_ready=0;changed=0;target=433;yields=0;
    CTL=3;MUL=0x01240901;CPU=BUS=0x01ff01ff;
    assert(apply()==0&&matches()&&yields==54);
    assert(ratio_count==3&&ratio_writes[0]==0x83&&ratio_writes[1]==0x84&&ratio_writes[2]==0x85);
    for(int i=0;i<3;i++)assert(ratio_multipliers[i]==0x01240901);
    assert(restore()==0);

    interfere=2;suspended=0;
    assert(apply()==-2&&suspended&&!locked);
    unsigned int prior=MUL;assert(restore()==-2&&MUL==prior);
    suspended=0;interfere=3;running=1;
    assert(apply()==-2&&!running&&!locked);
    assert(restore()==0); /* Worker shutdown still restores its own clock. */
    interfere=0;running=1;changed=0;
    CTL=2;prior=MUL;assert(apply()==-4&&MUL==prior);
    CTL=5;MUL=0x01240801;prior=MUL;assert(apply()==-4&&MUL==prior);
    MUL=0x01240901;CPU=0;assert(apply()==-4);
    CPU=BUS=0x01ff01ff;target=65;assert(apply()==-4);
    target=472;assert(apply()==-4);
    const char *boundaries[]={"clock_raw_guard_ready","clock_raw_ratio_3_ready",
        "clock_raw_ratio_4_ready","clock_raw_ratio_5_ready","clock_raw_multiplier_ready"};
    for(unsigned i=0;i<5;i++) {
        CTL=3;MUL=0x01240901;CPU=BUS=0x01ff01ff;
        target=433;changed=0;ratio_count=0;mutate_at=boundaries[i];
        assert(apply()==-3&&!locked);
        assert(ratio_count==(i<4?(int)i:3));
        assert(restore()==-3); /* Never acquire foreign state after logging. */
    }
    mutate_at=0;
    for(fail_ratio=3;fail_ratio<=5;fail_ratio++) {
        CTL=3;MUL=0x01240901;CPU=BUS=0x01ff01ff;
        target=433;changed=0;ratio_count=0;multiplier_calls=0;
        assert(apply()==-1&&!locked&&multiplier_calls==0);
        assert(MUL==0x01240901&&ratio_count==fail_ratio-2);
    }
    fail_ratio=0;changed=0;ratio_count=0;
    CTL=4;MUL=0x01240901;
    assert(apply()==0&&matches()&&ratio_count==2);
    assert(ratio_multipliers[0]==0x01240901&&ratio_multipliers[1]==0x01240901);
    assert(snapshots>0&&!locked);
    assert(sony_calls==0); /* Profiles/restore never call Sony. */
    /* Only startup gets the Sony baseline. Exact inherited tuple from logs. */
    for(int mode=0;mode<5;mode++) {
        CTL=3;MUL=0x0124E114;CPU=BUS=0x01ff01ff;
        changed=0;target=418;ratio_count=0;multiplier_calls=0;
        running=1;suspended=0;sony_mode=mode;
        int before=sony_calls,r=startup_apply();
        assert(sony_calls==before+1 && !locked);
        if(mode==0){assert(!r && matches());assert(restore()==0);}
        else {
            assert(r==(mode==1?-4:mode==2?-77:-2));
            assert(!multiplier_calls); /* Never force the raw path after failure. */
        }
    }
    /* Patched no-op with a safe state retains the proven raw fallback. */
    running=1;suspended=0;changed=0;sony_mode=1;ratio_count=0;
    CTL=3;MUL=0x01240901;CPU=BUS=0x01ff01ff;
    assert(startup_apply()==0 && matches() && ratio_count==3);
    assert(restore()==0);
    changed=0;CTL=5;MUL=0x0124E114;
    assert(startup_apply()==0 && matches());assert(restore()==0);
    changed=0;CTL=2;int before=sony_calls;
    assert(startup_apply()==-4 && sony_calls==before);
    CTL=3;MUL=0x01240801;
    assert(startup_apply()==-4 && sony_calls==before);
    MUL=0x01240901;suspended=1;assert(startup_apply()==-2 && sony_calls==before);
    suspended=0;running=0;assert(startup_apply()==-2 && sony_calls==before);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'ramp.c'
            path.write_text(harness)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'), str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
