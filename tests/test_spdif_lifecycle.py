"""Compile the actual optical worker with a small fake I2S driver."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STUB = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "streammaster/protocol.h"
#include "streammaster/spdif_protocol.h"
#define SM_GENERIC_BOARD 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_INTERNAL 8
#define GPIO_NUM_12 12
#define GPIO_MODE_OUTPUT 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_INVALID_ARG 258
#define I2S_NUM_AUTO 0
#define I2S_ROLE_MASTER 0
#define I2S_GPIO_UNUSED -1
#define I2S_DATA_BIT_WIDTH_32BIT 32
#define I2S_SLOT_MODE_STEREO 2
#define ESP_LOGE(...) ((void)0)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
typedef int esp_err_t,portMUX_TYPE;
typedef void *i2s_chan_handle_t;
typedef struct {void *dma_buf;size_t size;} i2s_event_data_t;
typedef struct {unsigned dma_desc_num,dma_frame_num;} i2s_chan_config_t;
typedef struct {unsigned rate;} i2s_std_clk_config_t;
typedef struct {i2s_std_clk_config_t clk_cfg; int slot_cfg; struct {int mclk,bclk,ws,dout,din;} gpio_cfg;} i2s_std_config_t;
typedef struct {bool (*on_sent)(i2s_chan_handle_t,i2s_event_data_t *,void *);} i2s_event_callbacks_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) ((i2s_chan_config_t){0})
#define I2S_STD_CLK_DEFAULT_CONFIG(r) {r}
#define I2S_STD_MSB_SLOT_DEFAULT_CONFIG(a,b) 0
static int allocations,inits,enabled,clock_changes,deletes,fail_mode;
static unsigned loaded_bytes;
static void *heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}
static size_t heap_caps_get_free_size(int c){(void)c;return 100000;}
static size_t heap_caps_get_largest_free_block(int c){return heap_caps_get_free_size(c);}
static void gpio_set_direction(int p,int m){(void)p;(void)m;}
static void gpio_set_level(int p,int v){(void)p;(void)v;}
static int i2s_new_channel(const i2s_chan_config_t *c,void **h,void *unused){(void)c;(void)unused;allocations++;*h=(void *)1;return 0;}
static int i2s_channel_init_std_mode(void *h,const i2s_std_config_t *c){(void)h;(void)c;inits++;return fail_mode?ESP_ERR_NO_MEM:0;}
static int i2s_channel_reconfig_std_clock(void *h,const i2s_std_clk_config_t *c){(void)h;assert(!enabled);assert(c->rate==88200||c->rate==96000);clock_changes++;return 0;}
static int i2s_channel_register_event_callback(void *h,const i2s_event_callbacks_t *c,void *u){(void)h;(void)c;(void)u;return 0;}
static int i2s_channel_preload_data(void *h,const void *p,size_t n,size_t *loaded){(void)h;(void)p;assert(!enabled);loaded_bytes+=n;*loaded=n;return 0;}
static int i2s_channel_enable(void *h){(void)h;assert(!enabled);enabled=1;return 0;}
static int i2s_channel_disable(void *h){(void)h;assert(enabled);enabled=0;return 0;}
static int i2s_del_channel(void *h){(void)h;assert(!enabled);deletes++;return 0;}
'''
MAIN = r'''
int main(void) {
    SmAudioOpen settings={48000,0};
    assert(open_output(&settings)==0 && enabled);
    Sample *saved_ring=ring;
    unsigned old_session=session;
    wr=100;rd=12;completed=77;position=1234;
    SmFrame r={.op=SM_AUDIO_CLOSE,.length=4},reply={0};
    memcpy(r.payload,&session,4);
    assert(sm_audio_command(&r,&reply)==0 && !enabled);
    assert(output && ring==saved_ring && disconnected);
    r.op=SM_AUDIO_STATUS;
    assert(sm_audio_command(&r,&reply)==SM_OFFLINE);
    settings.rate=44100;settings.non_audio=1;
    assert(open_output(&settings)==0 && enabled);
    assert(session!=old_session && !wr && !rd && !completed && !position);
    assert(paused && !disconnected && ring==saved_ring);
    assert(allocations==1 && inits==1 && clock_changes==1);
    assert(loaded_bytes==2*6144);
    assert(sm_audio_command(&r,&reply)==SM_OFFLINE); /* stale session */
    close_output();assert(!ring && !output && !enabled && deletes==1);
    fail_mode=1;assert(open_output(&settings)==SM_IO);
    assert(!output && !ring && !enabled);
    fail_mode=0;assert(open_output(&settings)==0);
    close_output();return 0;
}
'''


class Lifecycle(unittest.TestCase):
    def test_reuse_reset_rate_change_and_failed_init(self):
        source = (ROOT / 'streammaster/main/spdif.c').read_text()
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder)
            (path / 'test.c').write_text(STUB + source + MAIN)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT), str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
