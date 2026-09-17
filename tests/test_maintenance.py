import subprocess
import tempfile
import unittest
from pathlib import Path

from psp_streamer.server import Library, MediaItem

ROOT = Path(__file__).resolve().parents[1]


class MaintenanceTests(unittest.TestCase):
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
    OcConfig c={0,333,0,1}; int keys,line;
    assert(parse("# header\nenabled=1\ntarget_mhz=383\nenforce=1\nreport=1\n",&c,&keys,&line)==0);
    assert(c.enabled==1 && c.target==383 && c.enforce==1 && c.report==1 && keys==4 && !line);
    assert(parse("\xef\xbb\xbf  enabled = 0\r\n\ttarget_mhz = 443 ; note\r\nreport=0",&c,&keys,&line)==0);
    assert(!c.enabled && c.target==443 && !c.report && !c.enforce && keys==3);
    const char *bad[]={"", "# no settings\n", "enabled=1\ntarget_mhz=999", "enabled=2",
        "enabled=1\nreport=0\ntarget_mhz=332", "target_mhz=9999999999999999999999",
        "enabled=", "enforce=-1", "report=1oops", "typo=1", "enabled 1"};
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
        assert(oc_config_parse(file,n,&c,&keys,&line)==0 && keys==4);
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
        code = (ROOT / 'psp-overclock/main.c').read_text()
        code = code[code.index('static int matches(void);'):code.index('static int power_callback(')]
        harness = '''
#include <assert.h>
#include "clock_math.h"
static unsigned int CTL=5,MUL=0,CPU=0x01ff01ff,BUS=0x01ff01ff;
static int running=1,suspended,changed,target=443,fail_ready,interfere,yields;
#define SYNC() ((void)0)
static void settle(void) {}
static int ready(void) {CTL&=~0x80;return fail_ready?-1:0;}
static int sceKernelCpuSuspendIntr(void) {return 1;}
static void sceKernelCpuResumeIntr(int n) {assert(n==1);}
static int scePowerSetClockFrequency(int p,int c,int b) {assert(p==333&&c==333&&b==166);return 0;}
static void multiplier(unsigned int n) {MUL=(MUL&0xffff0000)|(n<<8)|OC_DEN;}
static void sceKernelDelayThreadCB(int n) {assert(n==10000);yields++;if(interfere)CPU=0x00800100;}
''' + code + '''
int main(void) {
    assert(oc_supported_model(2));
    CPU=0x00800100; BUS=0x00800100;
    assert(apply()==0 && changed && matches() && yields==59);
    assert(oc_khz(CTL,MUL,CPU)==442150);
    restore(); assert(((MUL>>8)&255)==180);
    target=333;assert(apply()==0&&matches());
    target=443;interfere=1;yields=0;
    assert(apply()==-3 && yields==1); /* abort before a second write */
    interfere=0;suspended=1;assert(apply()==-2);
    suspended=0;fail_ready=1;assert(apply()==-1);
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
