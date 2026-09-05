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
static unsigned char *hw_avcc;
static unsigned char hw_sps[256], hw_pps[256];
static int hw_sps_size, hw_pps_size, hw_ready;
static int hw_first_access_unit;
static int hw_output_stride = 512;
static int hw_output_height = 272;
static int hw_mpeg_mode = 4;
static const char *hw_step = "Hardware-AVC";

static int start_code_size(const unsigned char *data, int size, int at) {
    if (at + 3 < size && data[at] == 0 && data[at + 1] == 0 && data[at + 2] == 1) return 3;
    if (at + 4 < size && data[at] == 0 && data[at + 1] == 0 && data[at + 2] == 0 && data[at + 3] == 1) return 4;
    return 0;
}

static int next_start_code(const unsigned char *data, int size, int from) {
    int i;
    for (i = from; i + 3 < size; ++i) if (start_code_size(data, size, i)) return i;
    return size;
}

static int make_avcc(const unsigned char *au, int size) {
    int at = next_start_code(au, size, 0), written = 0;
    while (at < size) {
        int prefix = start_code_size(au, size, at);
        int payload = at + prefix;
        int next = next_start_code(au, size, payload);
        int length = next - payload;
        int type;
        if (length <= 0) { at = next; continue; }
        type = au[payload] & 0x1f;
        if (type == 7 && length <= (int)sizeof(hw_sps)) { memcpy(hw_sps, au + payload, length); hw_sps_size = length; }
        else if (type == 8 && length <= (int)sizeof(hw_pps)) { memcpy(hw_pps, au + payload, length); hw_pps_size = length; }
        else if (type != 9) {
            if (written + 4 + length > 768 * 1024) return -2;
            hw_avcc[written++] = (unsigned char)(length >> 24);
            hw_avcc[written++] = (unsigned char)(length >> 16);
            hw_avcc[written++] = (unsigned char)(length >> 8);
            hw_avcc[written++] = (unsigned char)length;
            memcpy(hw_avcc + written, au + payload, length);
            written += length;
        }
        at = next;
    }
    return written;
}

int h264_hw_init_from_annexb(const unsigned char *access_unit, int size) {
    int result, state_size;
    if (hw_ready) return 0;
    hw_avcc = memalign(64, 768 * 1024);
    if (!hw_avcc) return -2;
    make_avcc(access_unit, size);
    if (!hw_sps_size || !hw_pps_size) return -1;
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

int h264_hw_decode_annexb(const unsigned char *access_unit, int size, void *framebuffer) {
    AvcNalInput input;
    AvcDetail *detail = NULL;
    AvcCsc csc;
    int data_size, result;
    SceInt32 pictures = 0;
    if (!hw_ready) return -10;
    data_size = make_avcc(access_unit, size);
    if (data_size <= 0) return data_size;
    memset(&input, 0, sizeof(input));
    input.sps = hw_sps; input.sps_size = hw_sps_size;
    input.pps = hw_pps; input.pps_size = hw_pps_size;
    input.nal_length_bytes = 4; input.data = hw_avcc; input.data_size = data_size;
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
    hw_step = "AVC: Detail";
    result = sceMpegAvcDecodeDetail2(&hw_mpeg, &detail);
    if (result < 0 || !detail || !detail->picture || !detail->yuv) return result < 0 ? result : -11;
    memset(&csc, 0, sizeof(csc));
    csc.height_blocks = (detail->picture->height + 15) >> 4;
    csc.width_blocks = (detail->picture->width + 15) >> 4;
    memcpy(csc.plane, detail->yuv->plane, sizeof(csc.plane));
    hw_step = "AVC: Hardware-CSC";
    result = sceMpegBaseCscAvc(framebuffer, 0, hw_output_stride, &csc);
    if (result < 0) return result;
    sceKernelDcacheWritebackInvalidateRange(framebuffer, hw_output_stride * hw_output_height * 4);
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
    if (hw_avcc) free(hw_avcc);
    if (hw_workspace) free(hw_workspace);
    hw_state = hw_avcc = hw_workspace = NULL;
    hw_sps_size = hw_pps_size = 0;
    hw_output_stride = 512;
    hw_output_height = 272;
    hw_mpeg_mode = 4;
    hw_first_access_unit = 0;
    hw_step = "Hardware-AVC";
}

const char *h264_hw_last_step(void) { return hw_step; }
