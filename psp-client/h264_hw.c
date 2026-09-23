#include <pspkernel.h>
#include <pspmpeg.h>
#include <pspmpegbase.h>
#include <psputils.h>
#include <malloc.h>
#include <string.h>

#include "h264_hw.h"

/* These two sceMpeg entry points are present in retail firmware but absent
 * from older PSPSDK headers.  Their ABI is the same C ABI as sceMpeg. */
typedef struct {
    void *sps; int sps_size;
    void *pps; int pps_size;
    int nal_length_bytes;
    void *data; int data_size;
    int mode;
} AvcNalInput;

typedef struct { int unused0, unused1, width, height, unused[6]; } AvcPictureInfo;
typedef struct { void *plane[8]; int unused[3]; } AvcYuvInfo;
typedef struct {
    int unused0, unused1, unused2, unused3;
    AvcPictureInfo *picture;
    int unused4[6];
    AvcYuvInfo *yuv;
    int unused5[12];
} AvcDetail;
typedef struct {
    int height_blocks, width_blocks, mode0, mode1;
    void *plane[8];
} AvcCsc;

extern int sceMpegGetAvcNalAu(SceMpeg *mpeg, AvcNalInput *input, SceMpegAu *au);
extern int sceMpegAvcDecodeDetail2(SceMpeg *mpeg, AvcDetail **detail);
extern int sceMpegBaseCscAvc(void *destination, int unknown, int stride, AvcCsc *csc);

static SceMpeg hw_mpeg;
static SceMpegAu hw_au __attribute__((aligned(64)));
static SceMpegRingbuffer hw_ring __attribute__((aligned(64)));
static unsigned char *hw_workspace;
static unsigned char *hw_state;
static int hw_ready;
static int hw_first_access_unit;
static int hw_output_stride = 512;
static int hw_output_height = 272;
static int hw_mpeg_mode = 4;
static const char *hw_step = "Hardware-AVC";

int h264_hw_init_avcc(const AvcPacket *packet, int size) {
    int result, state_size;
    if (hw_ready) return 0;
    hw_step = "AVC: packet config";
    if (!packet || !avcc_packet_valid(packet,size) || ((uintptr_t)packet&63)) return -1;
    hw_workspace = memalign(0x400000, 0x200000);
    if (!hw_workspace) return -2;
    memset(&hw_ring, 0, sizeof(hw_ring));
    result = sceMpegInit();
    hw_step = "AVC: sceMpegInit";
    if (result < 0) return result;
    state_size = sceMpegQueryMemSize(hw_mpeg_mode);
    if (state_size < 0) return state_size;
    hw_state = memalign(64, state_size);
    if (!hw_state) return -3;
    hw_step = "AVC: sceMpegCreate";
    result = sceMpegCreate(&hw_mpeg, hw_state, state_size, &hw_ring, 512, hw_mpeg_mode, (SceInt32)hw_workspace);
    if (result < 0) return result;
    memset(&hw_au, 0xff, sizeof(hw_au));
    hw_step = "AVC: sceMpegInitAu";
    result = sceMpegInitAu(&hw_mpeg, hw_workspace + 0x10000, &hw_au);
    if (result < 0) return result;
    hw_ready = 1;
    hw_first_access_unit = 1;
    return 0;
}

int h264_hw_decode_avcc(const AvcPacket *packet, int size, void *framebuffer) {
    AvcNalInput input;
    AvcDetail *detail = NULL;
    AvcCsc csc;
    int result;
    SceInt32 pictures = 0;
    if (!hw_ready) return -10;
    hw_step = "AVC: packet bounds";
    if (!packet || !avcc_packet_valid(packet,size) || ((uintptr_t)packet&63)) return -1;
    memset(&input, 0, sizeof(input));
    input.sps = (void *)packet->config.sps; input.sps_size = packet->config.sps_size;
    input.pps = (void *)packet->config.pps; input.pps_size = packet->config.pps_size;
    input.nal_length_bytes = 4; input.data = (void *)packet->data; input.data_size = packet->data_size;
    /* Mode 3 announces the first IDR access unit to sceMpeg.  Subsequent
     * samples use mode 0, as in the native MP4/AVC path. */
    input.mode = hw_first_access_unit ? 3 : 0;
    sceKernelDcacheWritebackInvalidateAll();
    hw_step = "AVC: GetNalAu";
    result = sceMpegGetAvcNalAu(&hw_mpeg, &input, &hw_au);
    if (result < 0) return result;
    hw_step = "AVC: Decode";
    result = sceMpegAvcDecode(&hw_mpeg, &hw_au, hw_output_stride, NULL, &pictures);
    if (result < 0 || pictures <= 0) return result;
    hw_first_access_unit = 0;
    /* PPA discards already decoded late pictures without changing scanout.
     * Keep AVC reference state, but skip CSC entirely for a dropped picture. */
    if (!framebuffer) return pictures;
    hw_step = "AVC: Detail";
    result = sceMpegAvcDecodeDetail2(&hw_mpeg, &detail);
    if (result < 0 || !detail || !detail->picture || !detail->yuv) return result < 0 ? result : -11;
    memset(&csc, 0, sizeof(csc));
    csc.height_blocks = (detail->picture->height + 15) >> 4;
    csc.width_blocks = (detail->picture->width + 15) >> 4;
    memcpy(csc.plane, detail->yuv->plane, sizeof(csc.plane));
    hw_step = "AVC: Hardware-CSC";
    /* The destination may be a reusable RAM staging frame. Flush old CPU
     * overlays BEFORE DMA, never over the newly converted picture. */
    sceKernelDcacheWritebackInvalidateRange(framebuffer, hw_output_stride * hw_output_height * 4);
    result = sceMpegBaseCscAvc(framebuffer, 0, hw_output_stride, &csc);
    if (result < 0) return result;
    sceKernelDcacheInvalidateRange(framebuffer, hw_output_stride * hw_output_height * 4);
    return pictures;
}

void h264_hw_set_output_layout(int stride, int height, int mpeg_mode) {
    if (!hw_ready && stride >= 512 && !(stride & 15) && height >= 272 &&
        (mpeg_mode == 4 || mpeg_mode == 5)) {
        hw_output_stride = stride;
        hw_output_height = height;
        hw_mpeg_mode = mpeg_mode;
    }
}

void h264_hw_shutdown(void) {
    if (hw_ready) { sceMpegDelete(&hw_mpeg); sceMpegFinish(); }
    hw_ready = 0;
    if (hw_state) free(hw_state);
    if (hw_workspace) free(hw_workspace);
    hw_state = hw_workspace = NULL;
    hw_output_stride = 512;
    hw_output_height = 272;
    hw_mpeg_mode = 4;
    hw_first_access_unit = 0;
    hw_step = "Hardware-AVC";
}

const char *h264_hw_last_step(void) { return hw_step; }
